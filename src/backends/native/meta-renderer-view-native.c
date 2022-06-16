/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2020 Dor Askayo
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

#include "backends/native/meta-renderer-view-native.h"

#include "clutter/clutter.h"
#include "backends/meta-output.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-output-kms.h"

typedef enum _MetaFrameSyncMode
{
  META_FRAME_SYNC_MODE_INIT,
  META_FRAME_SYNC_MODE_ENABLED,
  META_FRAME_SYNC_MODE_DISABLED
} MetaFrameSyncMode;

struct _MetaRendererViewNative
{
  MetaRendererView parent;

  gboolean frame_sync_mode_update_queued;

  MetaFrameSyncMode frame_sync_mode;
  ClutterActor *frame_sync_actor;

  gulong frame_sync_actor_frozen_id;
  gulong frame_sync_actor_destroy_id;
};

G_DEFINE_TYPE (MetaRendererViewNative, meta_renderer_view_native,
               META_TYPE_RENDERER_VIEW);

static void
on_frame_sync_actor_frozen (ClutterActor           *actor,
                            MetaRendererViewNative *view_native)
{
  meta_renderer_view_native_set_frame_sync_actor (view_native, NULL);
}

static void
on_frame_sync_actor_destroyed (ClutterActor           *actor,
                               MetaRendererViewNative *view_native)
{
  meta_renderer_view_native_set_frame_sync_actor (view_native, NULL);
}

static void
meta_renderer_view_native_schedule_actor_update (ClutterStageView *stage_view,
                                                 ClutterActor     *actor)
{
  MetaRendererViewNative *view_native = META_RENDERER_VIEW_NATIVE (stage_view);
  ClutterFrameClock *frame_clock;

  g_return_if_fail (actor != NULL);

  frame_clock = clutter_stage_view_get_frame_clock (stage_view);

  if (view_native->frame_sync_mode == META_FRAME_SYNC_MODE_ENABLED &&
      actor == view_native->frame_sync_actor)
    clutter_frame_clock_schedule_update_now (frame_clock);
  else
    clutter_frame_clock_schedule_update (frame_clock);
}

void
meta_renderer_view_native_set_frame_sync_actor (MetaRendererViewNative *view_native,
                                                ClutterActor           *actor)
{
  if (G_LIKELY (actor == view_native->frame_sync_actor))
    return;

  if (view_native->frame_sync_actor)
    {
      g_clear_signal_handler (&view_native->frame_sync_actor_frozen_id,
                              view_native->frame_sync_actor);
      g_clear_signal_handler (&view_native->frame_sync_actor_destroy_id,
                              view_native->frame_sync_actor);
    }

  if (actor)
    {
      view_native->frame_sync_actor_frozen_id =
      g_signal_connect (actor, "frozen",
                        G_CALLBACK (on_frame_sync_actor_frozen),
                        view_native);
      view_native->frame_sync_actor_destroy_id =
      g_signal_connect (actor, "destroy",
                        G_CALLBACK (on_frame_sync_actor_destroyed),
                        view_native);
    }

  view_native->frame_sync_actor = actor;

  view_native->frame_sync_mode_update_queued = TRUE;
}

static void
meta_renderer_view_native_set_frame_sync (MetaRendererViewNative *view_native,
                                          MetaOutput             *output,
                                          MetaFrameSyncMode       sync_mode)
{
  ClutterFrameClock *frame_clock =
    clutter_stage_view_get_frame_clock (CLUTTER_STAGE_VIEW (view_native));
  MetaOutputKms *output_kms = META_OUTPUT_KMS (output);

  switch (sync_mode)
    {
    case META_FRAME_SYNC_MODE_ENABLED:
      clutter_frame_clock_set_mode (frame_clock,
                                    CLUTTER_FRAME_CLOCK_MODE_VARIABLE);
      meta_output_kms_set_vrr_mode (output_kms, TRUE);
      meta_output_kms_set_ie_mode (output_kms, TRUE);
      break;
    case META_FRAME_SYNC_MODE_DISABLED:
      clutter_frame_clock_set_mode (frame_clock,
                                    CLUTTER_FRAME_CLOCK_MODE_FIXED);
      meta_output_kms_set_vrr_mode (output_kms, FALSE);
      meta_output_kms_set_ie_mode (output_kms, FALSE);
      break;
    case META_FRAME_SYNC_MODE_INIT:
      g_assert_not_reached ();
    }

  view_native->frame_sync_mode = sync_mode;
}

static MetaFrameSyncMode
meta_renderer_view_native_get_applicable_sync_mode (MetaRendererViewNative *view_native)
{
  MetaRendererView *view = META_RENDERER_VIEW (view_native);
  MetaOutput *output = meta_renderer_view_get_output (view);

  if (view_native->frame_sync_actor != NULL &&
      meta_output_is_vrr_enabled (output))
    return META_FRAME_SYNC_MODE_ENABLED;
  else
    return META_FRAME_SYNC_MODE_DISABLED;
}

void
meta_renderer_view_native_maybe_set_frame_sync (MetaRendererViewNative *view_native)
{
  MetaRendererView *view;
  MetaOutput *output;
  MetaFrameSyncMode applicable_sync_mode;

  if (G_LIKELY (!view_native->frame_sync_mode_update_queued))
    return;

  view_native->frame_sync_mode_update_queued = FALSE;

  view = META_RENDERER_VIEW (view_native);
  output = meta_renderer_view_get_output (view);

  if (!meta_output_is_vrr_capable (output))
    return;

  if (!meta_output_is_ie_capable (output))
    return;

  applicable_sync_mode =
    meta_renderer_view_native_get_applicable_sync_mode (view_native);

  if (applicable_sync_mode != view_native->frame_sync_mode)
    {
      meta_renderer_view_native_set_frame_sync (view_native,
                                                output,
                                                applicable_sync_mode);
    }
}

static void
meta_renderer_view_native_dispose (GObject *object)
{
  MetaRendererViewNative *view_native = META_RENDERER_VIEW_NATIVE (object);

  if (view_native->frame_sync_actor)
    {
      g_clear_signal_handler (&view_native->frame_sync_actor_destroy_id,
                              view_native->frame_sync_actor);
      g_clear_signal_handler (&view_native->frame_sync_actor_frozen_id,
                              view_native->frame_sync_actor);
    }

  G_OBJECT_CLASS (meta_renderer_view_native_parent_class)->dispose (object);
}

static void
meta_renderer_view_native_init (MetaRendererViewNative *view_native)
{
  view_native->frame_sync_mode_update_queued = TRUE;
  view_native->frame_sync_mode = META_FRAME_SYNC_MODE_INIT;
  view_native->frame_sync_actor = NULL;
  view_native->frame_sync_actor_frozen_id = 0;
  view_native->frame_sync_actor_destroy_id = 0;
}

static void
meta_renderer_view_native_class_init (MetaRendererViewNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterStageViewClass *clutter_stage_view_class = CLUTTER_STAGE_VIEW_CLASS (klass);

  object_class->dispose = meta_renderer_view_native_dispose;

  clutter_stage_view_class->schedule_actor_update = meta_renderer_view_native_schedule_actor_update;
}
