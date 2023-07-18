/*
 * Copyright (C) 2018 Endless, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#include "config.h"

#include "compositor/clutter-utils.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "compositor/region-utils.h"
#include "meta/meta-window-actor.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-window-wayland.h"

struct _MetaSurfaceContainerActorWayland
{
  ClutterActor parent;

  MetaWindowActor *window_actor;
};

static void surface_container_cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaSurfaceContainerActorWayland,
                         meta_surface_container_actor_wayland,
                         CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE,
                                                surface_container_cullable_iface_init))

struct _MetaWindowActorWayland
{
  MetaWindowActor parent;
  ClutterActor *background;
  MetaSurfaceContainerActorWayland *surface_container;
  gulong highest_scale_monitor_handler_id;
};

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaWindowActorWayland, meta_window_actor_wayland,
                         META_TYPE_WINDOW_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE,
                                                cullable_iface_init))

typedef struct _SurfaceTreeTraverseData
{
  ClutterActor *surface_container;
  int index;
} SurfaceTreeTraverseData;

static MetaSurfaceContainerActorWayland *
surface_container_new (MetaWindowActor *window_actor)
{
  MetaSurfaceContainerActorWayland *surface_container;

  surface_container = g_object_new (META_TYPE_SURFACE_CONTAINER_ACTOR_WAYLAND,
                                    NULL);
  surface_container->window_actor = window_actor;

  return surface_container;
}

static void
surface_container_cull_out (MetaCullable   *cullable,
                            cairo_region_t *unobscured_region,
                            cairo_region_t *clip_region)
{
  meta_cullable_cull_out_children (cullable, unobscured_region, clip_region);
}

static void
surface_container_reset_culling (MetaCullable *cullable)
{
  meta_cullable_reset_culling_children (cullable);
}

static void
surface_container_cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = surface_container_cull_out;
  iface->reset_culling = surface_container_reset_culling;
}

static void
surface_container_apply_transform (ClutterActor      *actor,
                                   graphene_matrix_t *matrix)
{
  ClutterActor *parent = clutter_actor_get_parent (actor);
  ClutterActorClass *parent_class =
    CLUTTER_ACTOR_CLASS (meta_surface_container_actor_wayland_parent_class);
  MetaWindow *window;
  MetaLogicalMonitor *logical_monitor;
  MetaRectangle monitor_rect;
  float scale;
  float rel_x, rel_y;
  float abs_x, abs_y;
  float adj_rel_x, adj_rel_y;
  float x_off, y_off;

  parent_class->apply_transform (actor, matrix);

  if (!parent)
    return;

  window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (parent));
  if (!window)
    return;

  logical_monitor = meta_window_get_highest_scale_monitor (window);
  if (!logical_monitor)
    return;

  scale = meta_logical_monitor_get_scale (logical_monitor);
  monitor_rect = meta_logical_monitor_get_layout (logical_monitor);

  abs_x = clutter_actor_get_x (parent) + clutter_actor_get_x (actor);
  abs_y = clutter_actor_get_y (parent) + clutter_actor_get_y (actor);

  rel_x = abs_x - monitor_rect.x;
  rel_y = abs_y - monitor_rect.y;

  adj_rel_x = roundf (rel_x * scale) / scale;
  adj_rel_y = roundf (rel_y * scale) / scale;

  x_off = adj_rel_x - rel_x;
  y_off = adj_rel_y - rel_y;

  if (!G_APPROX_VALUE (x_off, 0.0, FLT_EPSILON) ||
      !G_APPROX_VALUE (y_off, 0.0, FLT_EPSILON))
    {
      graphene_matrix_translate (matrix,
                                 &GRAPHENE_POINT3D_INIT (x_off, y_off, 0));
    }
}

static void
surface_container_dispose (GObject *object)
{
  MetaSurfaceContainerActorWayland *self = META_SURFACE_CONTAINER_ACTOR_WAYLAND (object);

  clutter_actor_remove_all_children (CLUTTER_ACTOR (self));

  G_OBJECT_CLASS (meta_surface_container_actor_wayland_parent_class)->dispose (object);
}

static void
meta_surface_container_actor_wayland_class_init (MetaSurfaceContainerActorWaylandClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  actor_class->apply_transform = surface_container_apply_transform;

  object_class->dispose = surface_container_dispose;
}

static void
meta_surface_container_actor_wayland_init (MetaSurfaceContainerActorWayland *self)
{
}

static gboolean
get_surface_actor_list (GNode    *node,
                        gpointer  data)
{
  MetaWaylandSurface *surface = node->data;
  MetaSurfaceActor *surface_actor = meta_wayland_surface_get_actor (surface);
  GList **surface_actors = data;

  *surface_actors = g_list_prepend (*surface_actors, surface_actor);
  return FALSE;
}

static gboolean
set_surface_actor_index (GNode    *node,
                         gpointer  data)
{
  MetaWaylandSurface *surface = node->data;
  SurfaceTreeTraverseData *traverse_data = data;
  ClutterActor *container = traverse_data->surface_container;
  ClutterActor *surface_actor =
    CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));

  if (clutter_actor_contains (container, surface_actor))
    {
      if (clutter_actor_get_child_at_index (container, traverse_data->index) !=
          surface_actor)
        {
          clutter_actor_set_child_at_index (container,
                                            surface_actor,
                                            traverse_data->index);
        }
    }
  else
    {
      clutter_actor_insert_child_at_index (container,
                                           surface_actor,
                                           traverse_data->index);
    }
  traverse_data->index++;

  return FALSE;
}

void
meta_window_actor_wayland_rebuild_surface_tree (MetaWindowActor *actor)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  MetaSurfaceActor *surface_actor =
    meta_window_actor_get_surface (actor);
  MetaWaylandSurface *surface = meta_surface_actor_wayland_get_surface (
    META_SURFACE_ACTOR_WAYLAND (surface_actor));
  GNode *root_node = surface->output_state.subsurface_branch_node;
  g_autoptr (GList) surface_actors = NULL;
  g_autoptr (GList) children = NULL;
  GList *l;
  SurfaceTreeTraverseData traverse_data;

  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   get_surface_actor_list,
                   &surface_actors);

  children =
    clutter_actor_get_children (CLUTTER_ACTOR (self->surface_container));
  for (l = children; l; l = l->next)
    {
      ClutterActor *child_actor = l->data;

      if (!g_list_find (surface_actors, child_actor))
        {
          clutter_actor_remove_child (CLUTTER_ACTOR (self->surface_container),
                                      child_actor);
        }
    }

  traverse_data = (SurfaceTreeTraverseData) {
    .surface_container = CLUTTER_ACTOR (self->surface_container),
    .index = 0,
  };
  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   set_surface_actor_index,
                   &traverse_data);
}

static cairo_region_t *
calculate_background_cull_region (MetaWindowActorWayland *self)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (self);
  int geometry_scale;
  cairo_rectangle_int_t rect;

  geometry_scale = meta_window_actor_get_geometry_scale (window_actor);
  rect = (cairo_rectangle_int_t) {
    .x = 0,
    .y = 0,
    .width = clutter_actor_get_width (self->background) * geometry_scale,
    .height = clutter_actor_get_height (self->background) * geometry_scale,
  };

  return cairo_region_create_rectangle (&rect);
}

static void
meta_window_actor_wayland_cull_out (MetaCullable   *cullable,
                                    cairo_region_t *unobscured_region,
                                    cairo_region_t *clip_region)
{
  MetaWindowActorWayland *self =
    META_WINDOW_ACTOR_WAYLAND (cullable);

  meta_cullable_cull_out_children (META_CULLABLE (self),
                                   unobscured_region,
                                   clip_region);
  if (self->background &&
      clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self)) == 0xff)
    {
      cairo_region_t *background_cull_region;

      background_cull_region = calculate_background_cull_region (self);

      if (unobscured_region)
        cairo_region_subtract (unobscured_region, background_cull_region);
      if (clip_region)
        cairo_region_subtract (clip_region, background_cull_region);

      cairo_region_destroy (background_cull_region);
    }
}

static void
meta_window_actor_wayland_reset_culling (MetaCullable *cullable)
{
  meta_cullable_reset_culling_children (cullable);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = meta_window_actor_wayland_cull_out;
  iface->reset_culling = meta_window_actor_wayland_reset_culling;
}

static void
meta_window_actor_wayland_assign_surface_actor (MetaWindowActor  *actor,
                                                MetaSurfaceActor *surface_actor)
{
  MetaWindowActorClass *parent_class =
    META_WINDOW_ACTOR_CLASS (meta_window_actor_wayland_parent_class);

  g_warn_if_fail (!meta_window_actor_get_surface (actor));

  parent_class->assign_surface_actor (actor, surface_actor);

  meta_window_actor_wayland_rebuild_surface_tree (actor);
}

static void
meta_window_actor_wayland_frame_complete (MetaWindowActor  *actor,
                                          ClutterFrameInfo *frame_info,
                                          int64_t           presentation_time)
{
}

static void
meta_window_actor_wayland_queue_frame_drawn (MetaWindowActor *actor,
                                             gboolean         skip_sync_delay)
{
}

static void
meta_window_actor_wayland_before_paint (MetaWindowActor  *actor,
                                        ClutterStageView *stage_view)
{
}

static void
meta_window_actor_wayland_after_paint (MetaWindowActor  *actor,
                                       ClutterStageView *stage_view)
{
}

static void
meta_window_actor_wayland_queue_destroy (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_set_frozen (MetaWindowActor *actor,
                                      gboolean         frozen)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  ClutterActor *child;
  ClutterActorIter iter;

  clutter_actor_iter_init (&iter, CLUTTER_ACTOR (self->surface_container));
  while (clutter_actor_iter_next (&iter, &child))
    meta_surface_actor_set_frozen (META_SURFACE_ACTOR (child), frozen);
}

static void
meta_window_actor_wayland_update_regions (MetaWindowActor *actor)
{
}

static gboolean
meta_window_actor_wayland_can_freeze_commits (MetaWindowActor *actor)
{
  return FALSE;
}

static gboolean
meta_window_actor_wayland_is_single_surface_actor (MetaWindowActor *actor)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  ClutterActor *surface_container = CLUTTER_ACTOR (self->surface_container);

  return clutter_actor_get_n_children (surface_container) == 1 &&
         !self->background;
}

static gboolean
maybe_configure_black_background (MetaWindowActorWayland *self,
                                  float                  *surfaces_width,
                                  float                  *surfaces_height,
                                  float                  *background_width,
                                  float                  *background_height)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (self);
  MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
  MetaLogicalMonitor *logical_monitor;
  int geometry_scale;
  MetaRectangle fullscreen_layout;
  ClutterActor *child;
  ClutterActorIter iter;
  float max_width = 0;
  float max_height = 0;

  if (!meta_window_wayland_is_acked_fullscreen (META_WINDOW_WAYLAND (window)))
    return FALSE;

  geometry_scale = meta_window_actor_get_geometry_scale (window_actor);

  logical_monitor = meta_window_get_main_logical_monitor (window);
  if (!logical_monitor)
    return FALSE;

  fullscreen_layout = meta_logical_monitor_get_layout (logical_monitor);

  clutter_actor_iter_init (&iter, CLUTTER_ACTOR (self->surface_container));
  while (clutter_actor_iter_next (&iter, &child))
    {
      float child_width, child_height;

      clutter_actor_get_size (child, &child_width, &child_height);

      if (META_IS_SURFACE_ACTOR (child) &&
          meta_surface_actor_is_opaque (META_SURFACE_ACTOR (child)) &&
          G_APPROX_VALUE (clutter_actor_get_x (child), 0,
                          CLUTTER_COORDINATE_EPSILON) &&
          G_APPROX_VALUE (clutter_actor_get_y (child), 0,
                          CLUTTER_COORDINATE_EPSILON) &&
          G_APPROX_VALUE (child_width, fullscreen_layout.width,
                          CLUTTER_COORDINATE_EPSILON) &&
          G_APPROX_VALUE (child_height, fullscreen_layout.height,
                          CLUTTER_COORDINATE_EPSILON))
        return FALSE;

      max_width = MAX (max_width, child_width);
      max_height = MAX (max_height, child_height);
    }

  *surfaces_width = max_width;
  *surfaces_height = max_height;
  *background_width = window->rect.width / geometry_scale;
  *background_height = window->rect.height / geometry_scale;
  return TRUE;
}

static void
meta_window_actor_wayland_sync_geometry (MetaWindowActor     *actor,
                                         const MetaRectangle *actor_rect)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (actor);
  ClutterActor *surface_container = CLUTTER_ACTOR (self->surface_container);
  MetaWindow *window;
  float surfaces_width, surfaces_height;
  float background_width, background_height;

  window = meta_window_actor_get_meta_window (actor);
  if (window->unmanaging)
    return;

  if (maybe_configure_black_background (self,
                                        &surfaces_width, &surfaces_height,
                                        &background_width, &background_height))
    {
      int geometry_scale;
      int child_actor_width, child_actor_height;

      if (!self->background)
        {
          self->background = clutter_actor_new ();
          clutter_actor_set_background_color (self->background,
                                              CLUTTER_COLOR_Black);
          clutter_actor_set_reactive (self->background, TRUE);
          clutter_actor_insert_child_below (CLUTTER_ACTOR (self),
                                            self->background,
                                            NULL);
        }

      geometry_scale =
        meta_window_actor_get_geometry_scale (actor);
      child_actor_width = actor_rect->width / geometry_scale;
      child_actor_height = actor_rect->height / geometry_scale;

      clutter_actor_set_size (self->background,
                              background_width, background_height);
      clutter_actor_set_position (surface_container,
                                  (child_actor_width - surfaces_width) / 2,
                                  (child_actor_height - surfaces_height) / 2);
    }
  else if (self->background)
    {
      clutter_actor_set_position (surface_container, 0, 0);
      g_clear_pointer (&self->background, clutter_actor_destroy);
    }
}

static void
meta_window_actor_wayland_dispose (GObject *object)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (object);
  MetaWindow *window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (self));
  GObjectClass *parent_class =
    G_OBJECT_CLASS (meta_window_actor_wayland_parent_class);

  g_clear_signal_handler (&self->highest_scale_monitor_handler_id,
                          window);

  parent_class->dispose (object);
}

static void
meta_window_actor_wayland_constructed (GObject *object)
{
  MetaWindowActorWayland *self = META_WINDOW_ACTOR_WAYLAND (object);
  MetaWindow *window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (self));

  G_OBJECT_CLASS (meta_window_actor_wayland_parent_class)->constructed (object);

  self->highest_scale_monitor_handler_id =
    g_signal_connect_swapped (window, "highest-scale-monitor-changed",
                              G_CALLBACK (clutter_actor_notify_transform_invalid),
                              self->surface_container);
}

static void
meta_window_actor_wayland_class_init (MetaWindowActorWaylandClass *klass)
{
  MetaWindowActorClass *window_actor_class = META_WINDOW_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  window_actor_class->assign_surface_actor = meta_window_actor_wayland_assign_surface_actor;
  window_actor_class->frame_complete = meta_window_actor_wayland_frame_complete;
  window_actor_class->queue_frame_drawn = meta_window_actor_wayland_queue_frame_drawn;
  window_actor_class->before_paint = meta_window_actor_wayland_before_paint;
  window_actor_class->after_paint = meta_window_actor_wayland_after_paint;
  window_actor_class->queue_destroy = meta_window_actor_wayland_queue_destroy;
  window_actor_class->set_frozen = meta_window_actor_wayland_set_frozen;
  window_actor_class->update_regions = meta_window_actor_wayland_update_regions;
  window_actor_class->can_freeze_commits = meta_window_actor_wayland_can_freeze_commits;
  window_actor_class->sync_geometry = meta_window_actor_wayland_sync_geometry;
  window_actor_class->is_single_surface_actor = meta_window_actor_wayland_is_single_surface_actor;

  object_class->constructed = meta_window_actor_wayland_constructed;
  object_class->dispose = meta_window_actor_wayland_dispose;
}

static void
meta_window_actor_wayland_init (MetaWindowActorWayland *self)
{
  self->surface_container = surface_container_new (META_WINDOW_ACTOR (self));

  clutter_actor_add_child (CLUTTER_ACTOR (self),
                           CLUTTER_ACTOR (self->surface_container));

  g_signal_connect_swapped (self, "notify::allocation",
                            G_CALLBACK (clutter_actor_notify_transform_invalid),
                            self->surface_container);
}
