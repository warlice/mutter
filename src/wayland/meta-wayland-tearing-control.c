/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2024 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Naveen Kumar <naveen1.kumar@intel.com>
 */

/**
 * MetaWaylandTearingControl
 *
 * Handles passing Tearing Control in Wayland
 *
 * The MetaWaylandTearingControl namespace adds core support for tearing control
 * that are passed through from clients in Wayland (e.g.
 * using the tearing_control_staging_v1 protocol).
 */

#include "config.h"

#include "wayland/meta-wayland-tearing-control.h"

#include <wayland-server.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-versions.h"

#include "tearing-control-v1-server-protocol.h"

static GQuark quark_tearing_surface_data = 0;

typedef struct _MetaWaylandTearingSurface
{
  struct wl_resource *resource;
  MetaWaylandSurface *surface;
  gulong destroy_handler_id;
} MetaWaylandTearingSurface;

static void
wp_tearing_control_set_presentation_hint (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t            hint_value)
{
  MetaWaylandTearingSurface *tearing_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = tearing_surface->surface;
  MetaWaylandSurfaceState *pending;
  enum wp_tearing_control_v1_presentation_hint hint = hint_value;

  if (!surface)
    return;

  pending = meta_wayland_surface_get_pending_state (surface);
  switch (hint)
    {
    case WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC:
      pending->allow_async_presentation = TRUE;
      break;
    case WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC:
      pending->allow_async_presentation = FALSE;
      break;
    default:
      wl_resource_post_error (resource, WL_DISPLAY_ERROR_INVALID_METHOD,
                              "Invalid argument: unknown presentation hint");
      return;
    }

  pending->has_new_allow_async_presentation = pending->allow_async_presentation;
}

static void
wp_tearing_control_destroy (struct wl_client   *client,
                            struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wp_tearing_control_v1_interface tearing_surface_interface = {
  wp_tearing_control_set_presentation_hint,
  wp_tearing_control_destroy,
};

static void
wp_tearing_control_manager_destroy (struct wl_client   *client,
                                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
on_surface_destroyed (MetaWaylandSurface        *surface,
                      MetaWaylandTearingSurface *tearing_surface)
{
  MetaWaylandSurfaceState *pending;

  if (tearing_surface->surface)
    {
      g_object_steal_qdata (G_OBJECT (tearing_surface->surface),
                            quark_tearing_surface_data);
      g_clear_signal_handler (&tearing_surface->destroy_handler_id,
                              tearing_surface->surface);

      pending = meta_wayland_surface_get_pending_state (tearing_surface->surface);
      pending->allow_async_presentation = FALSE;
      tearing_surface->surface = NULL;
    }
}

static void
tearing_control_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandTearingSurface *tearing_surface = wl_resource_get_user_data (resource);

  on_surface_destroyed (tearing_surface->surface, tearing_surface);

  g_free (tearing_surface);
}

static void
wp_tearing_control_manager_get_tearing_control (struct wl_client   *client,
                                                struct wl_resource *resource,
                                                uint32_t            id,
                                                struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandTearingSurface *tearing_surface;

  tearing_surface = g_object_get_qdata (G_OBJECT (surface), quark_tearing_surface_data);

  if (tearing_surface)
    {
      wl_resource_post_error (resource,
                              WP_TEARING_CONTROL_MANAGER_V1_ERROR_TEARING_CONTROL_EXISTS,
                              "Surface already has a tearing controller");
      return;
    }

  tearing_surface = g_new0 (MetaWaylandTearingSurface, 1);
  tearing_surface->surface = surface;
  tearing_surface->resource = wl_resource_create (client,
                                                  &wp_tearing_control_v1_interface,
                                                  wl_resource_get_version (resource),
                                                  id);
  wl_resource_set_implementation (tearing_surface->resource,
                                  &tearing_surface_interface,
                                  tearing_surface,
                                  tearing_control_surface_destructor);

  tearing_surface->destroy_handler_id =
    g_signal_connect (tearing_surface->surface, "destroy",
                      G_CALLBACK (on_surface_destroyed),
                      tearing_surface);

  g_object_set_qdata (G_OBJECT (surface),
                      quark_tearing_surface_data,
                      tearing_surface);
}

static const struct wp_tearing_control_manager_v1_interface
tearing_control_manager_implementation = {
  wp_tearing_control_manager_destroy,
  wp_tearing_control_manager_get_tearing_control,
};

static void
bind_tearing_controller (struct wl_client *client,
                         void             *data,
                         uint32_t          version,
                         uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_tearing_control_manager_v1_interface,
                                 version, id);

  wl_resource_set_implementation (resource,
                                  &tearing_control_manager_implementation,
                                  NULL, NULL);
}

void
meta_wayland_tearing_controller_init (MetaWaylandCompositor *compositor)
{
  if (!wl_global_create (compositor->wayland_display,
                        &wp_tearing_control_manager_v1_interface,
                        META_WP_TEARING_CONTROL_V1_VERSION,
                        NULL,
                        bind_tearing_controller))
    {
      g_error ("Failed to register a global wp_tearing_control object");
      return;
    }

  quark_tearing_surface_data =
    g_quark_from_static_string ("-meta-wayland-tearing-surface-data");
}
