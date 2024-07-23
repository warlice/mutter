/*
 * Copyright (C) 2024 SUSE Software Solutions Germany GmbH
 * Copyright (C) 2024 Red Hat
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
 *     Joan Torres <joan.torres@suse.com>
 *     Sebastian Wick <sebastian.wick@redhat.com>
 */

#include "config.h"

#include "meta-wayland-color-management.h"

#include "backends/meta-color-device.h"
#include "backends/meta-color-manager.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-outputs.h"

#include "color-management-v1-server-protocol.h"

struct _MetaWaylandColorManager
{
  GObject parent;

  MetaWaylandCompositor *compositor;
  struct wl_global *global;

  gulong color_state_changed_handler_id;

  /* struct wl_resource* */
  GList *resources;
  /* MetaWaylandColorManagementOutput* */
  GList *outputs;
  /* MetaWaylandColorManagementFeedbackSurface* */
  GList *feedback_surfaces;
  /* MetaWaylandImageDescription* */
  GList *image_descriptions;
};

#define META_TYPE_WAYLAND_COLOR_MANAGER (meta_wayland_color_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandColorManager,
                      meta_wayland_color_manager,
                      META, WAYLAND_COLOR_MANAGER,
                      GObject)

G_DEFINE_TYPE (MetaWaylandColorManager,
               meta_wayland_color_manager,
               G_TYPE_OBJECT)

typedef struct _MetaWaylandColorManagementOutput
{
  MetaWaylandColorManager *color_manager;
  struct wl_resource *resource;

  MetaWaylandOutput *output;
  gulong output_destroyed_handler_id;
} MetaWaylandColorManagementOutput;

static GQuark quark_cm_surface_data = 0;

typedef struct _MetaWaylandColorManagementSurface
{
  MetaWaylandColorManager *color_manager;
  struct wl_resource *resource;

  MetaWaylandSurface *surface;

  gulong surface_destroyed_handler_id;
} MetaWaylandColorManagementSurface;

typedef struct _MetaWaylandColorManagementFeedbackSurface
{
  MetaWaylandColorManager *color_manager;
  struct wl_resource *resource;

  MetaWaylandSurface *surface;
  ClutterColorState *preferred_color_state;

  gulong surface_destroyed_handler_id;
  gulong surface_entered_output_handler_id;
  gulong surface_left_output_handler_id;
} MetaWaylandColorManagementFeedbackSurface;

typedef enum _MetaWaylandImageDescriptionState
{
  META_WAYLAND_IMAGE_DESCRIPTION_STATE_PENDING,
  META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY,
  META_WAYLAND_IMAGE_DESCRIPTION_STATE_FAILED,
} MetaWaylandImageDescriptionState;

typedef struct _MetaWaylandImageDescription
{
  MetaWaylandColorManager *color_manager;
  struct wl_resource *resource;
  MetaWaylandImageDescriptionState state;
  gboolean has_info;
  ClutterColorState *color_state;
} MetaWaylandImageDescription;

typedef struct _MetaWaylandCreatorParams
{
  MetaWaylandColorManager *color_manager;
  struct wl_resource *resource;

  ClutterColorspace colorspace;
  ClutterTransferFunction transfer_function;
} MetaWaylandCreatorParams;

static MetaMonitorManager *
get_monitor_manager (MetaWaylandColorManager *color_manager)
{
  MetaWaylandCompositor *compositor = color_manager->compositor;
  MetaBackend *backend = meta_context_get_backend (compositor->context);

  return meta_backend_get_monitor_manager (backend);
}

static ClutterContext *
get_clutter_context (MetaWaylandColorManager *color_manager)
{
  MetaWaylandCompositor *compositor = color_manager->compositor;
  MetaBackend *backend = meta_context_get_backend (compositor->context);

  return meta_backend_get_clutter_context (backend);
}

static MetaRenderer *
get_renderer (MetaWaylandColorManager *color_manager)
{
  MetaWaylandCompositor *compositor = color_manager->compositor;
  MetaBackend *backend = meta_context_get_backend (compositor->context);

  return meta_backend_get_renderer (backend);
}

static MetaColorManager *
get_meta_color_manager (MetaWaylandColorManager *color_manager)
{
  MetaWaylandCompositor *compositor = color_manager->compositor;
  MetaBackend *backend = meta_context_get_backend (compositor->context);

  return meta_backend_get_color_manager (backend);
}

static ClutterColorManager *
get_clutter_color_manager (MetaWaylandColorManager *color_manager)
{
  ClutterContext *clutter_context = get_clutter_context (color_manager);

  return clutter_context_get_color_manager (clutter_context);
}

static gboolean
wayland_tf_to_clutter (enum xx_color_manager_v4_transfer_function  tf,
                       ClutterTransferFunction                    *tf_out)
{
  switch (tf)
    {
    case XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB:
      *tf_out = CLUTTER_TRANSFER_FUNCTION_SRGB;
      return TRUE;
    case XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ:
      *tf_out = CLUTTER_TRANSFER_FUNCTION_PQ;
      return TRUE;
    default:
      return FALSE;
    }
}

static enum xx_color_manager_v4_transfer_function
clutter_tf_to_wayland (ClutterTransferFunction tf)
{
  switch (tf)
    {
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
      return XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB;
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ;
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_LINEAR;
    }
  g_assert_not_reached ();
}

static gboolean
wayland_primaries_to_clutter (enum xx_color_manager_v4_primaries  primaries,
                              ClutterColorspace                  *primaries_out)
{
  switch (primaries)
    {
    case XX_COLOR_MANAGER_V4_PRIMARIES_SRGB:
      *primaries_out = CLUTTER_COLORSPACE_SRGB;
      return TRUE;
    case XX_COLOR_MANAGER_V4_PRIMARIES_BT2020:
      *primaries_out = CLUTTER_COLORSPACE_BT2020;
      return TRUE;
    default:
      return FALSE;
    }
}

static enum xx_color_manager_v4_primaries
clutter_primaries_to_wayland (ClutterColorspace primaries)
{
  switch (primaries)
    {
    case CLUTTER_COLORSPACE_DEFAULT:
    case CLUTTER_COLORSPACE_SRGB:
      return XX_COLOR_MANAGER_V4_PRIMARIES_SRGB;
    case CLUTTER_COLORSPACE_BT2020:
      return XX_COLOR_MANAGER_V4_PRIMARIES_BT2020;
    }
  g_assert_not_reached ();
}

static ClutterColorState *
get_default_color_state (MetaWaylandColorManager *color_manager)
{
  ClutterColorManager *clutter_color_manager =
    get_clutter_color_manager (color_manager);
  ClutterColorState *color_state;

  color_state =
    clutter_color_manager_get_default_color_state (clutter_color_manager);
  return g_object_ref (color_state);
}

static ClutterColorState *
get_output_color_state (MetaWaylandColorManager *color_manager,
                        MetaMonitor             *monitor)
{
  MetaColorManager *meta_color_manager = get_meta_color_manager (color_manager);
  MetaColorDevice *color_device =
    meta_color_manager_get_color_device (meta_color_manager, monitor);

  if (!color_device)
    return get_default_color_state (color_manager);

  return meta_color_device_get_output_state (color_device);
}

static MetaWaylandImageDescription *
meta_wayland_image_description_new (MetaWaylandColorManager *color_manager,
                                    struct wl_resource      *resource)
{
  MetaWaylandImageDescription *image_desc;

  image_desc = g_new0 (MetaWaylandImageDescription, 1);
  image_desc->color_manager = color_manager;
  image_desc->resource = resource;

  color_manager->image_descriptions =
    g_list_prepend (color_manager->image_descriptions, image_desc);

  return image_desc;
}

static MetaWaylandImageDescription *
meta_wayland_image_description_new_failed (MetaWaylandColorManager            *color_manager,
                                           struct wl_resource                 *resource,
                                           enum xx_image_description_v4_cause  cause,
                                           const char                         *message)
{
  MetaWaylandImageDescription *image_desc =
    meta_wayland_image_description_new (color_manager, resource);

  image_desc->state = META_WAYLAND_IMAGE_DESCRIPTION_STATE_FAILED;
  image_desc->has_info = FALSE;
  xx_image_description_v4_send_failed (resource, cause, message);

  return image_desc;
}

static MetaWaylandImageDescription *
meta_wayland_image_description_new_color_state (MetaWaylandColorManager *color_manager,
                                                struct wl_resource      *resource,
                                                ClutterColorState       *color_state)
{
  MetaWaylandImageDescription *image_desc =
    meta_wayland_image_description_new (color_manager, resource);

  image_desc->state = META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY;
  image_desc->has_info = TRUE;
  image_desc->color_state = g_object_ref (color_state);
  xx_image_description_v4_send_ready (resource,
                                      clutter_color_state_get_id (color_state));

  return image_desc;
}

static void
meta_wayland_image_description_free (MetaWaylandImageDescription *image_desc)
{
  MetaWaylandColorManager *color_manager = image_desc->color_manager;

  g_clear_object (&image_desc->color_state);

  color_manager->image_descriptions =
    g_list_remove (color_manager->image_descriptions, image_desc);

  free (image_desc);
}

static void
image_description_destructor (struct wl_resource *resource)
{
  MetaWaylandImageDescription *image_desc = wl_resource_get_user_data (resource);

  meta_wayland_image_description_free (image_desc);
}

static void
image_description_destroy (struct wl_client   *client,
                           struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
send_information (struct wl_resource *info_resource,
                  ClutterColorState  *color_state)
{
  ClutterColorspace clutter_colorspace =
    clutter_color_state_get_colorspace (color_state);
  ClutterTransferFunction clutter_tf =
    clutter_color_state_get_transfer_function (color_state);
  enum xx_color_manager_v4_primaries primaries =
    clutter_primaries_to_wayland (clutter_colorspace);
  enum xx_color_manager_v4_transfer_function tf =
    clutter_tf_to_wayland (clutter_tf);

  xx_image_description_info_v4_send_primaries_named (info_resource, primaries);
  xx_image_description_info_v4_send_tf_named (info_resource, tf);
}

static void
image_description_get_information (struct wl_client   *client,
                                   struct wl_resource *resource,
                                   uint32_t            id)
{
  MetaWaylandImageDescription *image_desc = wl_resource_get_user_data (resource);
  struct wl_resource *info_resource;

  if (image_desc->state != META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_V4_ERROR_NOT_READY,
                              "The image description is not ready");
      return;
    }

  if (!image_desc->has_info)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_V4_ERROR_NO_INFORMATION,
                              "The image description has no information");
      return;
    }

  g_return_if_fail (image_desc->color_state);

  info_resource =
    wl_resource_create (client,
                        &xx_image_description_info_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  send_information (info_resource, image_desc->color_state);

  xx_image_description_info_v4_send_done (info_resource);
  wl_resource_destroy (info_resource);
}

static const struct xx_image_description_v4_interface
  meta_wayland_image_description_interface =
{
  image_description_destroy,
  image_description_get_information,
};

static MetaMonitor *
get_primary_monitor_for_surface (MetaWaylandColorManager *color_manager,
                                 MetaWaylandSurface      *surface)
{
  MetaRenderer *renderer = get_renderer (color_manager);
  MetaMonitorManager *monitor_manager = get_monitor_manager (color_manager);
  GList *logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  GList *l;
  MetaSurfaceActor *surface_actor;
  MetaMonitor *fallback_monitor = NULL;

  surface_actor = meta_wayland_surface_get_actor (surface);
  if (!surface_actor)
    return NULL;

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      GList *monitors = meta_logical_monitor_get_monitors (logical_monitor);
      GList *m;

      for (m = monitors; m; m = m->next)
        {
          MetaMonitor *monitor = m->data;
          MetaOutput *output = meta_monitor_get_main_output (monitor);
          MetaCrtc *crtc;
          MetaRendererView *renderer_view;
          ClutterStageView *stage_view;

          crtc = meta_output_get_assigned_crtc (output);
          if (!crtc)
            continue;

          renderer_view = meta_renderer_get_view_for_crtc  (renderer, crtc);
          if (!renderer_view)
            continue;

          stage_view = CLUTTER_STAGE_VIEW (renderer_view);

          if (clutter_actor_is_effectively_on_stage_view (CLUTTER_ACTOR (surface_actor),
                                                          stage_view))
            fallback_monitor = monitor;

          if (meta_surface_actor_wayland_is_view_primary (surface_actor,
                                                          stage_view))
            return monitor;
        }
    }

  return fallback_monitor;
}

static MetaMonitor *
get_primary_monitor (MetaWaylandColorManager *color_manager)
{
  MetaMonitorManager *monitor_manager = get_monitor_manager (color_manager);
  MetaLogicalMonitor *logical_monitor;
  GList *monitors;

  logical_monitor =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
  monitors = meta_logical_monitor_get_monitors (logical_monitor);

  if (monitors)
    return monitors->data;

  return NULL;
}

static void
update_preferred_color_state (MetaWaylandColorManagementFeedbackSurface *cm_feedback_surface)
{
  MetaWaylandColorManager *color_manager = cm_feedback_surface->color_manager;
  MetaWaylandSurface *surface = cm_feedback_surface->surface;
  MetaMonitor *monitor;
  g_autoptr (ClutterColorState) color_state = NULL;
  gboolean initial = !cm_feedback_surface->preferred_color_state;

  if (!surface)
    return;

  monitor = get_primary_monitor_for_surface (color_manager, surface);
  if (!monitor)
    monitor = get_primary_monitor (color_manager);

  color_state = get_output_color_state (color_manager, monitor);

  if (cm_feedback_surface->preferred_color_state &&
      clutter_color_state_equals (color_state,
                                  cm_feedback_surface->preferred_color_state))
    return;

  g_set_object (&cm_feedback_surface->preferred_color_state, color_state);

  if (!initial)
    xx_color_management_feedback_surface_v4_send_preferred_changed (cm_feedback_surface->resource);
}

static void
make_surface_inert (MetaWaylandColorManagementSurface *cm_surface)
{
  if (!cm_surface->surface)
    return;

  g_clear_signal_handler (&cm_surface->surface_destroyed_handler_id,
                          cm_surface->surface);
  cm_surface->surface = NULL;
}

static MetaWaylandColorManagementSurface *
meta_wayland_color_management_surface_new (MetaWaylandColorManager *color_manager,
                                           struct wl_resource      *resource,
                                           MetaWaylandSurface      *surface)
{
  MetaWaylandColorManagementSurface *cm_surface;

  cm_surface = g_new0 (MetaWaylandColorManagementSurface, 1);
  cm_surface->color_manager = color_manager;
  cm_surface->resource = resource;
  cm_surface->surface = surface;

  cm_surface->surface_destroyed_handler_id =
    g_signal_connect_swapped (surface, "destroy",
                              G_CALLBACK (make_surface_inert),
                              cm_surface);

  return cm_surface;
}

static void
meta_wayland_color_management_surface_free (MetaWaylandColorManagementSurface *cm_surface)
{
  make_surface_inert (cm_surface);
  free (cm_surface);
}

static void
set_image_description (MetaWaylandColorManagementSurface *cm_surface,
                       ClutterColorState                 *color_state)
{
  MetaWaylandColorManager *color_manager = cm_surface->color_manager;
  MetaWaylandSurface *surface = cm_surface->surface;
  MetaWaylandSurfaceState *pending =
    meta_wayland_surface_get_pending_state (surface);
  g_autoptr (ClutterColorState) default_color_state = NULL;

  if (!color_state)
    {
      default_color_state = get_default_color_state (color_manager);
      color_state = default_color_state;
    }

  g_assert (color_state);

  pending->has_new_color_state = TRUE;
  g_set_object (&pending->color_state, color_state);
}

static void
color_management_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);

  if (cm_surface->surface)
    {
      g_object_steal_qdata (G_OBJECT (cm_surface->surface),
                            quark_cm_surface_data);
      set_image_description (cm_surface, NULL);
    }

  meta_wayland_color_management_surface_free (cm_surface);
}

static void
color_management_surface_destroy (struct wl_client   *client,
                                  struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_management_surface_set_image_description (struct wl_client   *client,
                                                struct wl_resource *resource,
                                                struct wl_resource *image_desc_resource,
                                                uint32_t            render_intent)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);
  MetaWaylandImageDescription *image_desc =
    wl_resource_get_user_data (image_desc_resource);
  MetaWaylandSurface *surface = cm_surface->surface;

  if (!surface)
    {
      /* FIXME: should this error be more specific? */
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
                              "Underlying surface object has been destroyed");
      return;
    }

  if (!image_desc->color_state ||
      image_desc->state != META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY)
    {
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
                              "Trying to set an image description which is not ready");
      return;
    }

  switch (render_intent)
    {
    case XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL:
      break;
    default:
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_RENDER_INTENT,
                              "Trying to use an unsupported rendering intent");
      return;
    }

  set_image_description (cm_surface, image_desc->color_state);
}

static void
color_management_surface_unset_image_description (struct wl_client   *client,
                                                  struct wl_resource *resource)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = cm_surface->surface;

  if (!surface)
    {
      /* FIXME: should this error be more specific? */
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
                              "Underlying surface object has been destroyed");
      return;
    }

  set_image_description (cm_surface, NULL);
}

static const struct xx_color_management_surface_v4_interface
  meta_wayland_color_management_surface_interface =
{
  color_management_surface_destroy,
  color_management_surface_set_image_description,
  color_management_surface_unset_image_description,
};

static void
on_output_destroyed (MetaWaylandOutput                *wayland_output,
                     MetaWaylandColorManagementOutput *cm_output)
{
  g_clear_signal_handler (&cm_output->output_destroyed_handler_id,
                          cm_output->output);
  cm_output->output = NULL;
}

static MetaWaylandColorManagementOutput *
meta_wayland_color_management_output_new (MetaWaylandColorManager *color_manager,
                                          struct wl_resource      *resource,
                                          MetaWaylandOutput       *output)
{
  MetaWaylandColorManagementOutput *cm_output;

  cm_output = g_new0 (MetaWaylandColorManagementOutput, 1);
  cm_output->color_manager = color_manager;
  cm_output->resource = resource;
  cm_output->output = output;

  cm_output->output_destroyed_handler_id =
    g_signal_connect (output, "output-destroyed",
                      G_CALLBACK (on_output_destroyed),
                      cm_output);

  color_manager->outputs = g_list_prepend (color_manager->outputs, cm_output);

  return cm_output;
}

static void
meta_wayland_color_management_output_free (MetaWaylandColorManagementOutput *cm_output)
{
  MetaWaylandColorManager *color_manager = cm_output->color_manager;

  if (cm_output->output)
    {
      g_clear_signal_handler (&cm_output->output_destroyed_handler_id,
                              cm_output->output);
      cm_output->output = NULL;
    }

  color_manager->outputs = g_list_remove (color_manager->outputs, cm_output);

  free (cm_output);
}

static void
color_management_output_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManagementOutput *cm_output =
    wl_resource_get_user_data (resource);

  meta_wayland_color_management_output_free (cm_output);
}

static void
color_management_output_destroy (struct wl_client   *client,
                                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_management_output_get_image_description (struct wl_client   *client,
                                               struct wl_resource *resource,
                                               uint32_t            id)
{
  MetaWaylandColorManagementOutput *cm_output =
    wl_resource_get_user_data (resource);
  MetaWaylandColorManager *color_manager = cm_output->color_manager;
  struct wl_resource *image_desc_resource;
  MetaWaylandImageDescription *image_desc;

  image_desc_resource =
    wl_resource_create (client,
                        &xx_image_description_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  if (cm_output->output)
    {
      MetaMonitor *monitor = meta_wayland_output_get_monitor (cm_output->output);
      g_autoptr (ClutterColorState) color_state =
        get_output_color_state (color_manager, monitor);

      image_desc =
        meta_wayland_image_description_new_color_state (color_manager,
                                                        image_desc_resource,
                                                        color_state);
    }
  else
    {
      image_desc =
        meta_wayland_image_description_new_failed (color_manager,
                                                   image_desc_resource,
                                                   XX_IMAGE_DESCRIPTION_V4_CAUSE_NO_OUTPUT,
                                                  "Underlying output object has been destroyed");
    }

  wl_resource_set_implementation (image_desc_resource,
                                  &meta_wayland_image_description_interface,
                                  image_desc,
                                  image_description_destructor);
}

static const struct xx_color_management_output_v4_interface
  meta_wayland_color_management_output_interface =
{
  color_management_output_destroy,
  color_management_output_get_image_description,
};

static MetaWaylandCreatorParams *
meta_wayland_creator_params_new (MetaWaylandColorManager *color_manager,
                                 struct wl_resource      *resource)
{
  MetaWaylandCreatorParams *creator_params;

  creator_params = g_new0 (MetaWaylandCreatorParams, 1);
  creator_params->color_manager = color_manager;
  creator_params->resource = resource;

  creator_params->colorspace = CLUTTER_COLORSPACE_DEFAULT;
  creator_params->transfer_function = CLUTTER_TRANSFER_FUNCTION_DEFAULT;

  return creator_params;
}

static void
meta_wayland_creator_params_free (MetaWaylandCreatorParams *creator_params)
{
  free (creator_params);
}

static void
creator_params_destructor (struct wl_resource *resource)
{
  MetaWaylandCreatorParams *creator_params = wl_resource_get_user_data (resource);

  meta_wayland_creator_params_free (creator_params);
}

static void creator_params_create (struct wl_client   *client,
                                   struct wl_resource *resource,
                                   uint32_t            id)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  MetaWaylandColorManager *color_manager = creator_params->color_manager;
  ClutterContext *clutter_context = get_clutter_context (color_manager);
  struct wl_resource *image_desc_resource;
  g_autoptr (ClutterColorState) color_state = NULL;
  MetaWaylandImageDescription *image_desc;

  if (creator_params->colorspace == CLUTTER_COLORSPACE_DEFAULT ||
      creator_params->transfer_function == CLUTTER_TRANSFER_FUNCTION_DEFAULT)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INCOMPLETE_SET,
                              "Not all required parameters were set");
      return;
    }

  image_desc_resource =
    wl_resource_create (client,
                        &xx_image_description_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  color_state = clutter_color_state_new (clutter_context,
                                         creator_params->colorspace,
                                         creator_params->transfer_function);

  /* FIXME: should this have has_info = FALSE? */
  image_desc =
    meta_wayland_image_description_new_color_state (color_manager,
                                                    image_desc_resource,
                                                    color_state);

  wl_resource_set_implementation (image_desc_resource,
                                  &meta_wayland_image_description_interface,
                                  image_desc,
                                  image_description_destructor);

  wl_resource_destroy (resource);
}

static void
creator_params_set_tf_named (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            tf)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  ClutterTransferFunction clutter_tf;

  if (creator_params->transfer_function != CLUTTER_TRANSFER_FUNCTION_DEFAULT)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_ALREADY_SET,
                              "The transfer characteristics were already set");
      return;
    }

  if (!wayland_tf_to_clutter (tf, &clutter_tf))
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_TF,
                              "The named transfer characteristics are not supported");
      return;
    }

  creator_params->transfer_function = clutter_tf;
}

static void
creator_params_set_tf_power (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            eexp)
{
  wl_resource_post_error (resource,
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_TF,
                          "Setting power based transfer characteristics is not supported");
}

static void
creator_params_set_primaries_named (struct wl_client   *client,
                                    struct wl_resource *resource,
                                    uint32_t            primaries)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  ClutterColorspace colorspace;

  if (creator_params->colorspace != CLUTTER_COLORSPACE_DEFAULT)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_ALREADY_SET,
                              "The primaries were already set");
      return;
    }

  if (!wayland_primaries_to_clutter (primaries, &colorspace))
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_PRIMARIES,
                              "The named primaries are not supported");
      return;
    }

  creator_params->colorspace = colorspace;
}

static void creator_params_set_primaries (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          int32_t             r_x,
                                          int32_t             r_y,
                                          int32_t             g_x,
                                          int32_t             g_y,
                                          int32_t             b_x,
                                          int32_t             b_y,
                                          int32_t             w_x,
                                          int32_t             w_y)
{
  wl_resource_post_error (resource,
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_PRIMARIES,
                          "Setting arbitrary primaries is not supported");
}

static void creator_params_set_luminance (struct wl_client   *client,
                                          struct wl_resource *resource,
			                  uint32_t            min_lum,
			                  uint32_t            max_lum,
			                  uint32_t            reference_lum)
{
  wl_resource_post_error (resource,
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_LUMINANCE,
                          "Setting luminance is not supported");
}

static void creator_params_set_mastering_display_primaries (struct wl_client   *client,
                                                            struct wl_resource *resource,
                                                            int32_t             r_x,
                                                            int32_t             r_y,
                                                            int32_t             g_x,
                                                            int32_t             g_y,
                                                            int32_t             b_x,
                                                            int32_t             b_y,
                                                            int32_t             w_x,
                                                            int32_t             w_y)
{
  wl_resource_post_error (resource,
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_MASTERING,
                          "Setting mastering display primaries is not supported");
}

static void
creator_params_set_mastering_luminance (struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            min_lum,
                                        uint32_t            max_lum)
{
  wl_resource_post_error (resource,
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_MASTERING,
                          "Setting mastering display luminances is not supported");
}

static void
creator_params_set_max_cll (struct wl_client   *client,
                            struct wl_resource *resource,
                            uint32_t            max_cll)
{
  /* ignoring for now */
  /* FIXME: must send error in some cases */
}

static void creator_params_set_max_fall (struct wl_client   *client,
                                         struct wl_resource *resource,
                                         uint32_t            max_fall)
{
  /* ignoring for now */
  /* FIXME: must send error in some cases */
}

static const struct xx_image_description_creator_params_v4_interface
  meta_wayland_image_description_creator_params_interface =
{
  creator_params_create,
  creator_params_set_tf_named,
  creator_params_set_tf_power,
  creator_params_set_primaries_named,
  creator_params_set_primaries,
  creator_params_set_luminance,
  creator_params_set_mastering_display_primaries,
  creator_params_set_mastering_luminance,
  creator_params_set_max_cll,
  creator_params_set_max_fall,
};

static void
color_manager_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);

  color_manager->resources = g_list_remove (color_manager->resources, resource);
}

static void
color_manager_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_manager_get_output (struct wl_client   *client,
                          struct wl_resource *resource,
                          uint32_t            id,
                          struct wl_resource *output_resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandOutput *output = wl_resource_get_user_data (output_resource);
  MetaWaylandColorManagementOutput *cm_output;
  struct wl_resource *cm_output_resource;

  cm_output_resource =
    wl_resource_create (client,
                        &xx_color_management_output_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  cm_output = meta_wayland_color_management_output_new (color_manager,
                                                        cm_output_resource,
                                                        output);

  wl_resource_set_implementation (cm_output_resource,
                                  &meta_wayland_color_management_output_interface,
                                  cm_output,
                                  color_management_output_destructor);
}

static void
color_manager_get_surface (struct wl_client   *client,
                           struct wl_resource *resource,
                           uint32_t            id,
                           struct wl_resource *surface_resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandColorManagementSurface *cm_surface;
  struct wl_resource *cm_surface_resource;

  cm_surface = g_object_get_qdata (G_OBJECT (surface), quark_cm_surface_data);
  if (cm_surface)
    {
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGER_V4_ERROR_SURFACE_EXISTS,
                              "surface already requested");
      return;
    }

  cm_surface_resource =
    wl_resource_create (client,
                        &xx_color_management_surface_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  cm_surface = meta_wayland_color_management_surface_new (color_manager,
                                                          cm_surface_resource,
                                                          surface);

  wl_resource_set_implementation (cm_surface_resource,
                                  &meta_wayland_color_management_surface_interface,
                                  cm_surface,
                                  color_management_surface_destructor);

  g_object_set_qdata (G_OBJECT (surface), quark_cm_surface_data, cm_surface);
}

static void
make_feedback_surface_inert (MetaWaylandColorManagementFeedbackSurface *cm_feedback_surface)
{
  g_clear_signal_handler (&cm_feedback_surface->surface_destroyed_handler_id,
                          cm_feedback_surface->surface);
  g_clear_signal_handler (&cm_feedback_surface->surface_entered_output_handler_id,
                          cm_feedback_surface->surface);
  g_clear_signal_handler (&cm_feedback_surface->surface_left_output_handler_id,
                          cm_feedback_surface->surface);
  g_clear_object (&cm_feedback_surface->preferred_color_state);
  cm_feedback_surface->surface = NULL;
}

static MetaWaylandColorManagementFeedbackSurface *
meta_wayland_color_management_feedback_surface_new (MetaWaylandColorManager *color_manager,
                                                    struct wl_resource      *resource,
                                                    MetaWaylandSurface      *surface)
{
  MetaWaylandColorManagementFeedbackSurface *cm_feedback_surface;

  cm_feedback_surface = g_new0 (MetaWaylandColorManagementFeedbackSurface, 1);
  cm_feedback_surface->color_manager = color_manager;
  cm_feedback_surface->resource = resource;
  cm_feedback_surface->surface = surface;

  cm_feedback_surface->surface_destroyed_handler_id =
    g_signal_connect_swapped (surface, "destroy",
                              G_CALLBACK (make_feedback_surface_inert),
                              cm_feedback_surface);

  cm_feedback_surface->surface_entered_output_handler_id =
    g_signal_connect_swapped (surface, "entered-output",
                              G_CALLBACK (update_preferred_color_state),
                              cm_feedback_surface);

  cm_feedback_surface->surface_left_output_handler_id =
    g_signal_connect_swapped (surface, "left-output",
                              G_CALLBACK (update_preferred_color_state),
                              cm_feedback_surface);

  color_manager->feedback_surfaces =
    g_list_prepend (color_manager->feedback_surfaces,
                    cm_feedback_surface);

  return cm_feedback_surface;
}

static void
meta_wayland_color_management_feedback_surface_free (MetaWaylandColorManagementFeedbackSurface *cm_feedback_surface)
{
  MetaWaylandColorManager *color_manager = cm_feedback_surface->color_manager;

  if (cm_feedback_surface->surface)
    make_feedback_surface_inert (cm_feedback_surface);

  color_manager->feedback_surfaces =
    g_list_remove (color_manager->feedback_surfaces, cm_feedback_surface);

  free (cm_feedback_surface);
}

static void
color_management_feedback_surface_destroy (struct wl_client   *client,
                                           struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_management_feedback_surface_get_preferred (struct wl_client   *client,
                                                 struct wl_resource *resource,
                                                 uint32_t            id)
{
  MetaWaylandColorManagementFeedbackSurface *cm_feedback_surface =
    wl_resource_get_user_data (resource);
  MetaWaylandColorManager *color_manager = cm_feedback_surface->color_manager;
  MetaWaylandSurface *surface = cm_feedback_surface->surface;
  struct wl_resource *image_desc_resource;
  MetaWaylandImageDescription *image_desc;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_FEEDBACK_SURFACE_V4_ERROR_INERT,
                              "Underlying surface object has been destroyed");
      return;
    }

  image_desc_resource =
    wl_resource_create (client,
                        &xx_image_description_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  image_desc =
    meta_wayland_image_description_new_color_state (color_manager,
                                                    image_desc_resource,
                                                    cm_feedback_surface->preferred_color_state);

  wl_resource_set_implementation (image_desc_resource,
                                  &meta_wayland_image_description_interface,
                                  image_desc,
                                  image_description_destructor);
}

static const struct xx_color_management_feedback_surface_v4_interface
  meta_wayland_color_management_feedback_surface_interface =
{
  color_management_feedback_surface_destroy,
  color_management_feedback_surface_get_preferred,
};

static void
color_management_feedback_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManagementFeedbackSurface *cm_feedback_surface =
    wl_resource_get_user_data (resource);

  meta_wayland_color_management_feedback_surface_free (cm_feedback_surface);
}

static void
color_manager_get_feedback_surface (struct wl_client   *client,
                                    struct wl_resource *resource,
                                    uint32_t            id,
                                    struct wl_resource *surface_resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandColorManagementFeedbackSurface *cm_feedback_surface;
  struct wl_resource *cm_feedback_surface_resource;

  cm_feedback_surface_resource =
    wl_resource_create (client,
                        &xx_color_management_feedback_surface_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  cm_feedback_surface =
    meta_wayland_color_management_feedback_surface_new (color_manager,
                                                        cm_feedback_surface_resource,
                                                        surface);

  wl_resource_set_implementation (cm_feedback_surface_resource,
                                  &meta_wayland_color_management_feedback_surface_interface,
                                  cm_feedback_surface,
                                  color_management_feedback_surface_destructor);

  update_preferred_color_state (cm_feedback_surface);
}

static void
color_manager_new_icc_creator (struct wl_client   *client,
                               struct wl_resource *resource,
                               uint32_t            id)
{
  wl_resource_post_error (resource,
                          XX_COLOR_MANAGER_V4_ERROR_UNSUPPORTED_FEATURE,
                          "ICC-based image description creator is unsupported");
}

static void
color_manager_new_parametric_creator (struct wl_client   *client,
                                      struct wl_resource *resource,
                                      uint32_t            id)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandCreatorParams *creator_params;
  struct wl_resource *creator_resource;

  creator_resource =
    wl_resource_create (client,
                        &xx_image_description_creator_params_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  creator_params = meta_wayland_creator_params_new (color_manager,
                                                    creator_resource);

  wl_resource_set_implementation (creator_resource,
                                  &meta_wayland_image_description_creator_params_interface,
                                  creator_params,
                                  creator_params_destructor);
}

static void
color_manager_send_supported_events (struct wl_resource *resource)
{
  xx_color_manager_v4_send_supported_intent (resource,
                                             XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL);
  xx_color_manager_v4_send_supported_feature (resource,
                                              XX_COLOR_MANAGER_V4_FEATURE_PARAMETRIC);
  xx_color_manager_v4_send_supported_tf_named (resource,
                                               XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB);
  xx_color_manager_v4_send_supported_tf_named (resource,
                                               XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ);
  xx_color_manager_v4_send_supported_primaries_named (resource,
                                                      XX_COLOR_MANAGER_V4_PRIMARIES_SRGB);
  xx_color_manager_v4_send_supported_primaries_named (resource,
                                                      XX_COLOR_MANAGER_V4_PRIMARIES_BT2020);
}

static const struct xx_color_manager_v4_interface
  meta_wayland_color_manager_interface =
{
  color_manager_destroy,
  color_manager_get_output,
  color_manager_get_surface,
  color_manager_get_feedback_surface,
  color_manager_new_icc_creator,
  color_manager_new_parametric_creator,
};

static void
color_management_bind (struct wl_client *client,
                       void             *data,
                       uint32_t          version,
                       uint32_t          id)
{
  MetaWaylandColorManager *color_manager = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &xx_color_manager_v4_interface,
                                 version,
                                 id);

  wl_resource_set_implementation (resource,
                                  &meta_wayland_color_manager_interface,
                                  color_manager,
                                  color_manager_destructor);

  color_manager->resources = g_list_prepend (color_manager->resources,
                                             resource);

  color_manager_send_supported_events (resource);
}

static void
update_output_color_state (MetaWaylandColorManager *color_manager,
                           MetaMonitor             *monitor)
{
  GList *l;

  for (l = color_manager->outputs; l; l = l->next)
    {
      MetaWaylandColorManagementOutput *cm_output = l->data;
      MetaMonitor *cm_monitor =
        meta_wayland_output_get_monitor (cm_output->output);

      if (monitor != cm_monitor)
        continue;

      xx_color_management_output_v4_send_image_description_changed (cm_output->resource);
    }

  for (l = color_manager->feedback_surfaces; l; l = l->next)
    {
      MetaWaylandColorManagementFeedbackSurface *cm_feedback_surface = l->data;
      MetaWaylandSurface *surface = cm_feedback_surface->surface;
      MetaWaylandOutput *wayland_output;
      GHashTableIter iter;

      g_hash_table_iter_init (&iter, surface->outputs);
      while (g_hash_table_iter_next (&iter, (gpointer *) &wayland_output, NULL))
        {
          MetaMonitor *cm_monitor =
            meta_wayland_output_get_monitor (wayland_output);

          if (monitor != cm_monitor)
            continue;

          update_preferred_color_state (cm_feedback_surface);
          break;
        }
    }
}

static void
on_color_state_changed (MetaColorManager        *meta_color_manager,
                        MetaColorDevice         *color_device,
                        MetaWaylandColorManager *color_manager)
{
  MetaMonitor *monitor = meta_color_device_get_monitor (color_device);

  update_output_color_state (color_manager, monitor);
}

static void
meta_wayland_color_manager_dispose (GObject *object)
{
  MetaWaylandColorManager *color_manager = META_WAYLAND_COLOR_MANAGER (object);
  MetaColorManager *meta_color_manager = get_meta_color_manager (color_manager);

  g_clear_signal_handler (&color_manager->color_state_changed_handler_id,
                          meta_color_manager);

  if (color_manager->global)
    {
      wl_global_remove (color_manager->global);
      color_manager->global = NULL;
    }
}

static void
meta_wayland_color_manager_init (MetaWaylandColorManager *color_manager)
{
}

static void
meta_wayland_color_manager_class_init (MetaWaylandColorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_wayland_color_manager_dispose;

  quark_cm_surface_data =
    g_quark_from_static_string ("-meta-wayland-color-management-surface-data");
}

static MetaWaylandColorManager *
meta_wayland_color_manager_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandColorManager *color_manager;
  MetaColorManager *meta_color_manager;

  color_manager = g_object_new (META_TYPE_WAYLAND_COLOR_MANAGER, NULL);
  color_manager->compositor = compositor;

  meta_color_manager = get_meta_color_manager (color_manager);
  color_manager->color_state_changed_handler_id =
    g_signal_connect_object (meta_color_manager, "device-color-state-changed",
                             G_CALLBACK (on_color_state_changed),
                             color_manager, 0);

  color_manager->global =
    wl_global_create (compositor->wayland_display,
                      &xx_color_manager_v4_interface,
                      META_XX_COLOR_MANAGEMENT_VERSION,
                      color_manager,
                      color_management_bind);
  if (color_manager->global == NULL)
    g_error ("Failed to register a global wp_color_management object");

  return color_manager;
}

static void
ensure_enabled (MetaWaylandCompositor *compositor)
{
  MetaDebugControl *debug_control =
    meta_context_get_debug_control (compositor->context);
  MetaWaylandColorManager *color_manager = NULL;

  if (meta_debug_control_is_color_management_protocol_enabled (debug_control))
    color_manager = meta_wayland_color_manager_new (compositor);

  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-color-manager",
                          color_manager,
                          g_object_unref);
}

void
meta_wayland_init_color_management (MetaWaylandCompositor *compositor)
{
  MetaDebugControl *debug_control =
    meta_context_get_debug_control (compositor->context);

  g_signal_connect_data (debug_control, "notify::color-management-protocol",
                         G_CALLBACK (ensure_enabled),
                         compositor, NULL,
                         G_CONNECT_SWAPPED | G_CONNECT_AFTER);

  ensure_enabled (compositor);
}
