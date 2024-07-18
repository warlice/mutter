/*
 * Copyright (C) 2016 Red Hat Inc.
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
 */

/**
 * MetaRendererView:
 *
 * Renders (a part of) the global stage.
 *
 * A MetaRendererView object is responsible for rendering (a part of) the
 * global stage, or more precisely: the part that matches what can be seen on a
 * #MetaLogicalMonitor. By splitting up the rendering into different parts and
 * attaching it to a #MetaLogicalMonitor, we can do the rendering so that each
 * renderer view is responsible for applying the right #MetaMonitorTransform
 * and the right scaling.
 */

#include "config.h"

#include "backends/meta-renderer-view.h"

#include "backends/meta-color-device.h"
#include "backends/meta-crtc.h"
#include "backends/meta-renderer.h"
#include "clutter/clutter-mutter.h"
#include "core/boxes-private.h"

enum
{
  PROP_0,

  PROP_TRANSFORM,
  PROP_CRTC,
  PROP_COLOR_DEVICE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _MetaRendererViewPrivate
{
  MetaMonitorTransform transform;

  MetaCrtc *crtc;
  MetaColorDevice *color_device;
  gulong color_state_changed_handler_id;
} MetaRendererViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaRendererView, meta_renderer_view,
                            META_TYPE_STAGE_VIEW)

MetaMonitorTransform
meta_renderer_view_get_transform (MetaRendererView *view)
{
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  return priv->transform;
}

MetaCrtc *
meta_renderer_view_get_crtc (MetaRendererView *view)
{
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  return priv->crtc;
}

static void
meta_renderer_view_get_offscreen_transformation_matrix (ClutterStageView  *view,
                                                        graphene_matrix_t *matrix)
{
  MetaRendererView *renderer_view = META_RENDERER_VIEW (view);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (renderer_view);

  graphene_matrix_init_identity (matrix);

  meta_monitor_transform_transform_matrix (
    meta_monitor_transform_invert (priv->transform), matrix);
}

static void
meta_renderer_view_setup_offscreen_transform (ClutterStageView *view,
                                              CoglPipeline     *pipeline)
{
  graphene_matrix_t matrix;

  meta_renderer_view_get_offscreen_transformation_matrix (view, &matrix);
  cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);
}

static gboolean
meta_renderer_view_get_offscreen_transformation_is_rotated (ClutterStageView *view)
{
  MetaRendererView *renderer_view = META_RENDERER_VIEW (view);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (renderer_view);

  return meta_monitor_transform_is_rotated (priv->transform);
}

static void
meta_renderer_view_transform_rect_to_onscreen (ClutterStageView   *view,
                                               const MtkRectangle *src_rect,
                                               int                 dst_width,
                                               int                 dst_height,
                                               MtkRectangle       *dst_rect)
{
  MetaRendererView *renderer_view = META_RENDERER_VIEW (view);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (renderer_view);

  meta_rectangle_transform (src_rect,
                            priv->transform,
                            dst_width,
                            dst_height,
                            dst_rect);
}

static void
meta_renderer_view_set_transform (MetaRendererView     *view,
                                  MetaMonitorTransform  transform)
{
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  if (priv->transform == transform)
    return;

  if (meta_monitor_transform_is_rotated (priv->transform) !=
      meta_monitor_transform_is_rotated (transform))
    clutter_stage_view_invalidate_offscreen (CLUTTER_STAGE_VIEW (view));
  else
    clutter_stage_view_invalidate_offscreen_blit_pipeline (CLUTTER_STAGE_VIEW (view));

  priv->transform = transform;
}

static void
set_color_states (MetaRendererView *view)
{
  ClutterStageView *clutter_stage_view = CLUTTER_STAGE_VIEW (view);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);
  g_autoptr (ClutterColorState) view_color_state = NULL;
  g_autoptr (ClutterColorState) output_color_state = NULL;

  g_return_if_fail (priv->color_device != NULL);

  view_color_state = meta_color_device_get_view_state (priv->color_device);
  output_color_state = meta_color_device_get_output_state (priv->color_device);

  clutter_stage_view_set_color_state (clutter_stage_view,
                                      view_color_state);
  clutter_stage_view_set_output_color_state (clutter_stage_view,
                                             output_color_state);
}

static void
on_color_state_changed (MetaColorDevice  *color_device,
                        MetaRendererView *view)
{
  set_color_states (view);
}

static void
meta_renderer_view_constructed (GObject *object)
{
  MetaRendererView *view = META_RENDERER_VIEW (object);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  if (priv->color_device)
    {
      set_color_states (view);

      priv->color_state_changed_handler_id =
        g_signal_connect (priv->color_device, "color-state-changed",
                          G_CALLBACK (on_color_state_changed),
                          view);
    }

  G_OBJECT_CLASS (meta_renderer_view_parent_class)->constructed (object);
}

static void
meta_renderer_view_dispose (GObject *object)
{
  MetaRendererView *view = META_RENDERER_VIEW (object);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  if (priv->color_device)
    {
      g_clear_signal_handler (&priv->color_state_changed_handler_id,
                              priv->color_device);
    }

  g_clear_object (&priv->color_device);

  G_OBJECT_CLASS (meta_renderer_view_parent_class)->dispose (object);
}

static void
meta_renderer_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaRendererView *view = META_RENDERER_VIEW (object);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  switch (prop_id)
    {
    case PROP_TRANSFORM:
      g_value_set_uint (value, priv->transform);
      break;
    case PROP_CRTC:
      g_value_set_object (value, priv->crtc);
      break;
    case PROP_COLOR_DEVICE:
      g_value_set_object (value, priv->color_device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaRendererView *view = META_RENDERER_VIEW (object);
  MetaRendererViewPrivate *priv =
    meta_renderer_view_get_instance_private (view);

  switch (prop_id)
    {
    case PROP_TRANSFORM:
      meta_renderer_view_set_transform (view, g_value_get_uint (value));
      break;
    case PROP_CRTC:
      priv->crtc = g_value_get_object (value);
      break;
    case PROP_COLOR_DEVICE:
      g_set_object (&priv->color_device, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_view_init (MetaRendererView *view)
{
}

static void
meta_renderer_view_class_init (MetaRendererViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterStageViewClass *view_class = CLUTTER_STAGE_VIEW_CLASS (klass);

  view_class->setup_offscreen_transform =
    meta_renderer_view_setup_offscreen_transform;
  view_class->get_offscreen_transformation_matrix =
    meta_renderer_view_get_offscreen_transformation_matrix;
  view_class->get_offscreen_transformation_is_rotated =
    meta_renderer_view_get_offscreen_transformation_is_rotated;
  view_class->transform_rect_to_onscreen =
    meta_renderer_view_transform_rect_to_onscreen;

  object_class->constructed = meta_renderer_view_constructed;
  object_class->dispose = meta_renderer_view_dispose;
  object_class->get_property = meta_renderer_view_get_property;
  object_class->set_property = meta_renderer_view_set_property;

  obj_props[PROP_TRANSFORM] =
    g_param_spec_uint ("transform", NULL, NULL,
                       META_MONITOR_TRANSFORM_NORMAL,
                       META_MONITOR_TRANSFORM_FLIPPED_270,
                       META_MONITOR_TRANSFORM_NORMAL,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  obj_props[PROP_CRTC] =
    g_param_spec_object ("crtc", NULL, NULL,
                         META_TYPE_CRTC,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_COLOR_DEVICE] =
    g_param_spec_object ("color-device", NULL, NULL,
                         META_TYPE_COLOR_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
