/*
 * Copyright (C) 2023 Valve Corporation
 * Copyright (C) 2024 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <wayland-server.h>

#include "fifo-v1-server-protocol.h"
#include "wayland/meta-wayland-fifo.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-transaction.h"
#include "wayland/meta-wayland-versions.h"

typedef struct _MetaWaylandFifo
{
  GObject parent;

  MetaWaylandCompositor *compositor;
  GHashTable *barrier_surfaces;
  guint fallback_timeout_id;
} MetaWaylandFifo;

#define META_TYPE_WAYLAND_FIFO (meta_wayland_fifo_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandFifo,
                      meta_wayland_fifo,
                      META, WAYLAND_FIFO,
                      GObject)

G_DEFINE_FINAL_TYPE (MetaWaylandFifo,
                     meta_wayland_fifo,
                     G_TYPE_OBJECT)

typedef struct _MetaWaylandFifoSurface
{
  MetaWaylandSurface *surface;
  gulong destroy_handler_id;
} MetaWaylandFifoSurface;

static void ensure_fallback_timer (MetaWaylandFifo *fifo);

static MetaBackend *
get_backend (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor =
    meta_wayland_surface_get_compositor (surface);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);

  return meta_context_get_backend (context);
}

static ClutterStageView *
get_stage_view (MetaWaylandSurface *surface)
{
  MetaBackend *backend = get_backend (surface);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
  GList *monitors;
  MetaOutput *output;
  MetaCrtc *crtc;
  MetaRendererView *renderer_view;

  logical_monitor = meta_wayland_surface_get_main_monitor (surface);
  if (!logical_monitor)
    {
      logical_monitor =
        meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
    }

  if (!logical_monitor)
    return NULL;

  monitors = meta_logical_monitor_get_monitors (logical_monitor);
  g_return_val_if_fail (monitors != NULL, NULL);

  output = meta_monitor_get_main_output (monitors->data);
  crtc = meta_output_get_assigned_crtc (output);
  renderer_view = meta_renderer_get_view_for_crtc (renderer, crtc);

  return CLUTTER_STAGE_VIEW (renderer_view);
}

void
meta_wayland_fifo_barrier_applied (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor =
    meta_wayland_surface_get_compositor (surface);
  MetaWaylandFifo *fifo =
    g_object_get_data (G_OBJECT (compositor), "-meta-wayland-fifo");
  ClutterStageView *surface_stage_view = get_stage_view (surface);

  surface->fifo_barrier = TRUE;
  g_hash_table_insert (fifo->barrier_surfaces,
                       g_object_ref (surface), surface_stage_view);

  ensure_fallback_timer (fifo);

  if (surface_stage_view)
    clutter_stage_view_schedule_update (surface_stage_view);
  else
    g_warn_if_fail (fifo->fallback_timeout_id != 0);
}

static void
meta_wayland_fifo_barrier_unset (MetaWaylandFifo  *fifo,
                                 ClutterStageView *stage_view)
{
  GHashTableIter iter;
  MetaWaylandSurface *surface;
  ClutterStageView *surface_stage_view;

  g_hash_table_iter_init (&iter, fifo->barrier_surfaces);
  while (g_hash_table_iter_next (&iter,
                                 (gpointer *) &surface,
                                 (gpointer *) &surface_stage_view))
    {
      if (!surface_stage_view || surface_stage_view != stage_view)
        continue;

      surface->fifo_barrier = FALSE;
      g_hash_table_iter_remove (&iter);
      meta_wayland_transaction_consider_surface (surface);
    }

  ensure_fallback_timer (fifo);
}

static void
meta_wayland_fifo_barrier_unset_all (MetaWaylandFifo *fifo)
{
  GHashTableIter iter;
  MetaWaylandSurface *surface;

  g_hash_table_iter_init (&iter, fifo->barrier_surfaces);
  while (g_hash_table_iter_next (&iter, (gpointer *) &surface, NULL))
    {
      surface->fifo_barrier = FALSE;
      g_hash_table_iter_remove (&iter);
      meta_wayland_transaction_consider_surface (surface);
    }

  ensure_fallback_timer (fifo);
}

static void
fifo_destructor (struct wl_resource *resource)
{
  MetaWaylandFifoSurface *fifo_surf = wl_resource_get_user_data (resource);

  if (fifo_surf->surface)
    {
      g_object_set_data (G_OBJECT (fifo_surf->surface),
                         "-meta-wayland-fifo-surface", NULL);

      g_clear_signal_handler (&fifo_surf->destroy_handler_id,
                              fifo_surf->surface);
    }

  g_free (fifo_surf);
}

static void
fifo_destroy (struct wl_client   *client,
              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
set_barrier (struct wl_client   *client,
             struct wl_resource *resource)
{
  MetaWaylandFifoSurface *fifo_surf = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = fifo_surf->surface;
  MetaWaylandSurfaceState *pending;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_FIFO_V1_ERROR_SURFACE_DESTROYED,
                              "surface destroyed");
      return;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  pending->fifo_barrier = true;
}

static void
wait_barrier (struct wl_client   *client,
              struct wl_resource *resource)
{
  MetaWaylandFifoSurface *fifo_surf = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = fifo_surf->surface;
  MetaWaylandSurfaceState *pending;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_FIFO_V1_ERROR_SURFACE_DESTROYED,
                              "surface destroyed");
      return;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  pending->fifo_wait = true;
}

static const struct wp_fifo_v1_interface meta_wayland_fifo_interface =
{
  set_barrier,
  wait_barrier,
  fifo_destroy,
};

static void
fifo_manager_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
on_surface_destroyed (MetaWaylandSurface     *surface,
                      MetaWaylandFifoSurface *fifo_surf)
{
  fifo_surf->surface = NULL;
}

static void
fifo_manager_get_queue (struct wl_client   *client,
                        struct wl_resource *resource,
                        uint32_t            id,
                        struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandFifoSurface *fifo_surf;
  struct wl_resource *fifo_resource;

  fifo_surf = g_object_get_data (G_OBJECT (surface),
                                 "-meta-wayland-fifo-surface");

  if (fifo_surf)
    {
      wl_resource_post_error (resource,
                              WP_FIFO_MANAGER_V1_ERROR_FIFO_MANAGER_ALREADY_EXISTS,
                              "Fifo resource already exists on surface");
      return;
    }

  fifo_resource = wl_resource_create (client,
                                      &wp_fifo_v1_interface,
                                      wl_resource_get_version (resource),
                                      id);

  fifo_surf = g_new0 (MetaWaylandFifoSurface, 1);
  fifo_surf->surface = surface;

  fifo_surf->destroy_handler_id =
    g_signal_connect (surface,
                      "destroy",
                      G_CALLBACK (on_surface_destroyed),
                      fifo_surf);

  g_object_set_data (G_OBJECT (surface),
                     "-meta-wayland-fifo-surface", fifo_surf);

  wl_resource_set_implementation (fifo_resource,
                                  &meta_wayland_fifo_interface,
                                  fifo_surf,
                                  fifo_destructor);
}

static const struct wp_fifo_manager_v1_interface meta_wayland_fifo_manager_interface =
{
  fifo_manager_destroy,
  fifo_manager_get_queue,
};

static void
bind_fifo (struct wl_client *client,
           void             *data,
           uint32_t          version,
           uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_fifo_manager_v1_interface,
                                 version, id);

  wl_resource_set_implementation (resource,
                                  &meta_wayland_fifo_manager_interface,
                                  NULL, NULL);
}

static void
meta_wayland_fifo_dispose (GObject *object)
{
  MetaWaylandFifo *fifo = META_WAYLAND_FIFO (object);

  g_clear_pointer (&fifo->barrier_surfaces, g_hash_table_unref);
  g_clear_handle_id (&fifo->fallback_timeout_id, g_source_remove);

  G_OBJECT_CLASS (meta_wayland_fifo_parent_class)->dispose (object);
}

static void
meta_wayland_fifo_init (MetaWaylandFifo *fifo)
{
  fifo->barrier_surfaces =
    g_hash_table_new_full (NULL, NULL,
                           (GDestroyNotify) g_object_unref,
                           NULL);
}

static void
meta_wayland_fifo_class_init (MetaWaylandFifoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_wayland_fifo_dispose;
}

static gboolean
meta_wayland_fifo_fallback_dispatch (gpointer user_data)
{
  MetaWaylandFifo *fifo = META_WAYLAND_FIFO (user_data);

  meta_wayland_fifo_barrier_unset_all (fifo);

  return G_SOURCE_CONTINUE;
}

static void
ensure_fallback_timer (MetaWaylandFifo *fifo)
{
  MetaContext *context = meta_wayland_compositor_get_context (fifo->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *primary_logical_monitor =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
  gboolean has_primary_monitor, has_fifo_barriers, needs_fallback;

  has_primary_monitor = primary_logical_monitor != NULL;
  has_fifo_barriers = g_hash_table_size (fifo->barrier_surfaces) > 0;
  needs_fallback = !has_primary_monitor && has_fifo_barriers;

  if (needs_fallback && !fifo->fallback_timeout_id)
    {
      fifo->fallback_timeout_id =
        g_timeout_add_full (G_PRIORITY_DEFAULT,
                            33,
                            meta_wayland_fifo_fallback_dispatch,
                            fifo,
                            NULL);
    }
  else if (!needs_fallback && fifo->fallback_timeout_id)
    {
      g_clear_handle_id (&fifo->fallback_timeout_id, g_source_remove);
    }
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaWaylandFifo    *fifo)
{
  meta_wayland_fifo_barrier_unset_all (fifo);
}

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *stage_view,
                 ClutterFrame     *frame,
                 MetaWaylandFifo  *fifo)
{
  meta_wayland_fifo_barrier_unset (fifo, stage_view);
}

void
meta_wayland_init_fifo (MetaWaylandCompositor *compositor)
{

  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);
  g_autoptr (MetaWaylandFifo) fifo = NULL;

  if (wl_global_create (compositor->wayland_display,
                        &wp_fifo_manager_v1_interface,
                        META_WP_FIFO_V1_VERSION,
                        NULL,
                        bind_fifo) == NULL)
    g_error ("Failed to register a global fifo object");

  fifo = g_object_new (META_TYPE_WAYLAND_FIFO, NULL);
  fifo->compositor = compositor;

  ensure_fallback_timer (fifo);

  g_signal_connect_object (monitor_manager, "monitors-changed",
                           G_CALLBACK (on_monitors_changed),
                           fifo,
                           G_CONNECT_DEFAULT);

  g_signal_connect_object (stage, "after-update",
                           G_CALLBACK (on_after_update),
                           fifo,
                           G_CONNECT_DEFAULT);

  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-fifo",
                          g_steal_pointer (&fifo),
                          g_object_unref);
}
