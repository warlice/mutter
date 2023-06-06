/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2022 Dor Askayo
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
 *     Dor Askayo <dor.askayo@gmail.com>
 */

#include "config.h"

#include "compositor/meta-compositor-view-native.h"

#include "backends/meta-crtc.h"
#include "backends/native/meta-crtc-kms.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-window-actor-private.h"

#ifdef HAVE_WAYLAND
#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-surface.h"
#endif /* HAVE_WAYLAND */

struct _MetaCompositorViewNative
{
  MetaCompositorView parent;

#ifdef HAVE_WAYLAND
  MetaWaylandSurface *scanout_candidate;
#endif /* HAVE_WAYLAND */
};

G_DEFINE_TYPE (MetaCompositorViewNative, meta_compositor_view_native,
               META_TYPE_COMPOSITOR_VIEW)

#ifdef HAVE_WAYLAND
static void
update_scanout_candidate (MetaCompositorViewNative *view_native,
                          MetaWaylandSurface       *surface,
                          MetaCrtc                 *crtc)
{
  if (view_native->scanout_candidate &&
      view_native->scanout_candidate != surface)
    {
      meta_wayland_surface_set_scanout_candidate (view_native->scanout_candidate,
                                                  NULL);
      g_clear_weak_pointer (&view_native->scanout_candidate);
    }

  if (surface)
    {
      meta_wayland_surface_set_scanout_candidate (surface, crtc);
      g_set_weak_pointer (&view_native->scanout_candidate,
                          surface);
    }
}

static gboolean
find_scanout_candidate (MetaCompositorView  *compositor_view,
                        MetaCompositor      *compositor,
                        MetaCrtc           **crtc_out,
                        CoglOnscreen       **onscreen_out,
                        MetaWaylandSurface **surface_out)
{
  ClutterStageView *stage_view;
  MetaRendererView *renderer_view;
  MetaCrtc *crtc;
  CoglFramebuffer *framebuffer;
  ClutterActor *topmost_actor;
  MetaSurfaceActor *surface_actor;
  MetaRectangle view_rect;
  ClutterActorBox actor_box;
  MetaSurfaceActorWayland *surface_actor_wayland;
  MetaWaylandSurface *surface;
  MetaWindow *window;
  MetaWindowActor *window_actor;
  int geometry_scale;

  stage_view = meta_compositor_view_get_stage_view (compositor_view);
  renderer_view = META_RENDERER_VIEW (stage_view);

  crtc = meta_renderer_view_get_crtc (renderer_view);
  if (!META_IS_CRTC_KMS (crtc))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no KMS CRTC");
      return FALSE;
    }

  framebuffer = clutter_stage_view_get_onscreen (stage_view);
  if (!COGL_IS_ONSCREEN (framebuffer))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no onscreen framebuffer");
      return FALSE;
    }

  if (clutter_stage_view_has_shadowfb (stage_view))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: stage-view has shadowfb");
      return FALSE;
    }

  topmost_actor = clutter_stage_view_get_topmost_actor (stage_view);
  if (!META_IS_SURFACE_ACTOR (topmost_actor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: topmost actor (%p) not a"
                  " surface actor",
                  topmost_actor);
      return FALSE;
    }
  surface_actor = META_SURFACE_ACTOR (topmost_actor);

  if (!meta_surface_actor_is_opaque (surface_actor))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: surface actor not opaque");
      return FALSE;
    }

  if (!clutter_actor_get_paint_box (CLUTTER_ACTOR (surface_actor),
                                    &actor_box))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no actor paint-box");
      return FALSE;
    }

  clutter_stage_view_get_layout (stage_view, &view_rect);
  if (!G_APPROX_VALUE (actor_box.x1, view_rect.x,
                       CLUTTER_COORDINATE_EPSILON) ||
      !G_APPROX_VALUE (actor_box.y1, view_rect.y,
                       CLUTTER_COORDINATE_EPSILON) ||
      !G_APPROX_VALUE (actor_box.x2, view_rect.x + view_rect.width,
                       CLUTTER_COORDINATE_EPSILON) ||
      !G_APPROX_VALUE (actor_box.y2, view_rect.y + view_rect.height,
                       CLUTTER_COORDINATE_EPSILON))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: paint-box (%f,%f,%f,%f) does "
                  "not match stage-view layout (%d,%d,%d,%d)",
                  actor_box.x1, actor_box.y1,
                  actor_box.x2 - actor_box.x1, actor_box.y2 - actor_box.y1,
                  view_rect.x, view_rect.y, view_rect.width, view_rect.height);
      return FALSE;
    }

  surface_actor_wayland = META_SURFACE_ACTOR_WAYLAND (surface_actor);
  surface = meta_surface_actor_wayland_get_surface (surface_actor_wayland);
  if (!surface)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no surface");
      return FALSE;
    }

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no meta-window");
      return FALSE;
    }

  window_actor = meta_window_actor_from_window (window);
  if (!window_actor)
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: no top window actor");
      return FALSE;
    }

  if (clutter_actor_has_transitions (CLUTTER_ACTOR (window_actor)))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: window-actor has transition");
      return FALSE;
    }

  geometry_scale = meta_window_actor_get_geometry_scale (window_actor);
  if (!meta_wayland_surface_can_scanout_untransformed (surface,
                                                       renderer_view,
                                                       geometry_scale))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No direct scanout candidate: surface can not be scanned out "
                  "untransformed");
      return FALSE;
    }

  *crtc_out = crtc;
  *onscreen_out = COGL_ONSCREEN (framebuffer);
  *surface_out = surface;

  return TRUE;
}

static void
try_assign_next_scanout (MetaCompositorView *compositor_view,
                         CoglOnscreen       *onscreen,
                         MetaWaylandSurface *surface)
{
  ClutterStageView *stage_view;
  g_autoptr (CoglScanout) scanout = NULL;

  scanout = meta_wayland_surface_try_acquire_scanout (surface,
                                                      onscreen);
  if (!scanout)
    {
      meta_topic (META_DEBUG_RENDER,
                  "Could not acquire scanout");
      return;
    }

  stage_view = meta_compositor_view_get_stage_view (compositor_view);

  clutter_stage_view_assign_next_scanout (stage_view, scanout);
}

void
meta_compositor_view_native_maybe_assign_scanout (MetaCompositorViewNative *view_native,
                                                  MetaCompositor           *compositor)
{
  MetaCompositorView *compositor_view = META_COMPOSITOR_VIEW (view_native);
  MetaCrtc *crtc = NULL;
  CoglOnscreen *onscreen = NULL;
  MetaWaylandSurface *surface = NULL;
  gboolean candidate_found;

  candidate_found = find_scanout_candidate (compositor_view,
                                            compositor,
                                            &crtc,
                                            &onscreen,
                                            &surface);
  if (candidate_found)
    {
      try_assign_next_scanout (compositor_view,
                               onscreen,
                               surface);
    }

  update_scanout_candidate (view_native, surface, crtc);
}
#endif /* HAVE_WAYLAND */

MetaCompositorViewNative *
meta_compositor_view_native_new (ClutterStageView *stage_view)
{
  g_assert (stage_view != NULL);

  return g_object_new (META_TYPE_COMPOSITOR_VIEW_NATIVE,
                       "stage-view", stage_view,
                       NULL);
}

static void
meta_compositor_view_native_finalize (GObject *object)
{
#ifdef HAVE_WAYLAND
  MetaCompositorViewNative *view_native = META_COMPOSITOR_VIEW_NATIVE (object);

  g_clear_weak_pointer (&view_native->scanout_candidate);
#endif /* HAVE_WAYLAND */

  G_OBJECT_CLASS (meta_compositor_view_native_parent_class)->finalize (object);
}

static void
meta_compositor_view_native_class_init (MetaCompositorViewNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_compositor_view_native_finalize;
}

static void
meta_compositor_view_native_init (MetaCompositorViewNative *view_native)
{
}
