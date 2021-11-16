/*
 * Copyright (C) 2019 Red Hat Inc.
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

#include "clutter-build-config.h"

#include "clutter/clutter-frame-clock-private.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-timeline-private.h"
#include "cogl/cogl-trace.h"

enum
{
  DESTROY,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _ClutterFrameListener
{
  const ClutterFrameListenerIface *iface;
  gpointer user_data;
} ClutterFrameListener;

typedef struct _ClutterFrameClockPrivate
{
  ClutterFrameListener listener;

  int64_t frame_count;

  gboolean pending_reschedule;
  gboolean pending_reschedule_now;

  int inhibit_count;

  GList *timelines;

  gboolean destroyed;
} ClutterFrameClockPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterFrameClock, clutter_frame_clock,
                                     G_TYPE_OBJECT)

void
clutter_frame_clock_add_timeline (ClutterFrameClock *frame_clock,
                                  ClutterTimeline   *timeline)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  gboolean is_first;

  if (g_list_find (priv->timelines, timeline))
    return;

  is_first = !priv->timelines;

  priv->timelines = g_list_prepend (priv->timelines, timeline);

  if (is_first)
    clutter_frame_clock_schedule_update (frame_clock);
}

void
clutter_frame_clock_remove_timeline (ClutterFrameClock *frame_clock,
                                     ClutterTimeline   *timeline)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  priv->timelines = g_list_remove (priv->timelines, timeline);
}

static void
advance_timelines (ClutterFrameClock *frame_clock,
                   int64_t            time_us)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);
  GList *timelines;
  GList *l;

  /* we protect ourselves from timelines being removed during
   * the advancement by other timelines by copying the list of
   * timelines, taking a reference on them, iterating over the
   * copied list and then releasing the reference.
   *
   * we cannot simply take a reference on the timelines and still
   * use the list held by the master clock because the do_tick()
   * might result in the creation of a new timeline, which gets
   * added at the end of the list with no reference increase and
   * thus gets disposed at the end of the iteration.
   *
   * this implies that a newly added timeline will not be advanced
   * by this clock iteration, which is perfectly fine since we're
   * in its first cycle.
   *
   * we also cannot steal the frame clock timelines list because
   * a timeline might be removed as the direct result of do_tick()
   * and remove_timeline() would not find the timeline, failing
   * and leaving a dangling pointer behind.
   */

  timelines = g_list_copy (priv->timelines);
  g_list_foreach (timelines, (GFunc) g_object_ref, NULL);

  for (l = timelines; l; l = l->next)
    {
      ClutterTimeline *timeline = l->data;

      _clutter_timeline_do_tick (timeline, time_us / 1000);
    }

  g_list_free_full (timelines, g_object_unref);
}

void
clutter_frame_clock_maybe_reschedule_update (ClutterFrameClock *frame_clock)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  if (priv->pending_reschedule ||
      priv->timelines)
    {
      priv->pending_reschedule = FALSE;

      if (priv->pending_reschedule_now)
        {
          priv->pending_reschedule_now = FALSE;
          clutter_frame_clock_schedule_update_now (frame_clock);
        }
      else
        {
          clutter_frame_clock_schedule_update (frame_clock);
        }
    }
}

void
clutter_frame_clock_notify_presented (ClutterFrameClock *frame_clock,
                                      ClutterFrameInfo  *frame_info)
{
  CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->notify_presented (frame_clock,
                                                                 frame_info);
}

void
clutter_frame_clock_notify_ready (ClutterFrameClock *frame_clock)
{
  CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->notify_ready (frame_clock);
}

void
clutter_frame_clock_inhibit (ClutterFrameClock *frame_clock)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  priv->inhibit_count++;

  if (priv->inhibit_count == 1)
    CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->inhibited (frame_clock);
}

void
clutter_frame_clock_uninhibit (ClutterFrameClock *frame_clock)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  g_return_if_fail (priv->inhibit_count > 0);

  priv->inhibit_count--;

  if (priv->inhibit_count == 0)
    clutter_frame_clock_maybe_reschedule_update (frame_clock);
}

void
clutter_frame_clock_reschedule (ClutterFrameClock *frame_clock)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  priv->pending_reschedule = TRUE;
}

void
clutter_frame_clock_reschedule_now (ClutterFrameClock *frame_clock)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  priv->pending_reschedule = TRUE;
  priv->pending_reschedule_now = TRUE;
}

void
clutter_frame_clock_schedule_update_now (ClutterFrameClock *frame_clock)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  if (priv->inhibit_count > 0)
    {
      priv->pending_reschedule = TRUE;
      priv->pending_reschedule_now = TRUE;
      return;
    }

  CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->schedule_update_now (frame_clock);
}

void
clutter_frame_clock_schedule_update (ClutterFrameClock *frame_clock)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  if (priv->inhibit_count > 0)
    {
      priv->pending_reschedule = TRUE;
      return;
    }

  CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->schedule_update (frame_clock);
}

void
clutter_frame_clock_dispatch (ClutterFrameClock *frame_clock,
                              int64_t            time_us)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);
  int64_t frame_count;
  int64_t timeline_time_us;
  ClutterFrameResult result;

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockDispatch, "Frame Clock (dispatch)");

  timeline_time_us =
    CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->pre_dispatch (frame_clock,
                                                               time_us);

  frame_count = priv->frame_count++;

  COGL_TRACE_BEGIN (ClutterFrameClockEvents, "Frame Clock (before frame)");
  if (priv->listener.iface->before_frame)
    {
      priv->listener.iface->before_frame (frame_clock,
                                          frame_count,
                                          priv->listener.user_data);
    }
  COGL_TRACE_END (ClutterFrameClockEvents);

  COGL_TRACE_BEGIN (ClutterFrameClockTimelines, "Frame Clock (timelines)");
  advance_timelines (frame_clock, timeline_time_us);
  COGL_TRACE_END (ClutterFrameClockTimelines);

  COGL_TRACE_BEGIN (ClutterFrameClockFrame, "Frame Clock (frame)");
  result = priv->listener.iface->frame (frame_clock,
                                        frame_count,
                                        priv->listener.user_data);
  COGL_TRACE_END (ClutterFrameClockFrame);

  CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->post_dispatch (frame_clock,
                                                              result);
}

void
clutter_frame_clock_record_flip_time (ClutterFrameClock *frame_clock,
                                      int64_t            flip_time_us)
{
  CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->record_flip_time (frame_clock,
                                                                 flip_time_us);
}

int
clutter_frame_clock_get_priority (ClutterFrameClock *frame_clock)
{
  return CLUTTER_FRAME_CLOCK_GET_CLASS (frame_clock)->get_priority (frame_clock);
}

static void
clutter_frame_clock_dispose (GObject *object)
{
  ClutterFrameClock *frame_clock = CLUTTER_FRAME_CLOCK (object);
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  if (!priv->destroyed)
    {
      priv->destroyed = TRUE;
      g_signal_emit (frame_clock, signals[DESTROY], 0);
    }

  G_OBJECT_CLASS (clutter_frame_clock_parent_class)->dispose (object);
}

static void
clutter_frame_clock_init (ClutterFrameClock *frame_clock)
{
}

static void
clutter_frame_clock_class_init (ClutterFrameClockClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = clutter_frame_clock_dispose;

  signals[DESTROY] =
    g_signal_new (I_("destroy"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
}

void
clutter_frame_clock_destroy (ClutterFrameClock *frame_clock)
{
  g_object_run_dispose (G_OBJECT (frame_clock));
  g_object_unref (frame_clock);
}

void
clutter_frame_clock_set_listener (ClutterFrameClock               *frame_clock,
                                  const ClutterFrameListenerIface *iface,
                                  gpointer                         user_data)
{
  ClutterFrameClockPrivate *priv =
    clutter_frame_clock_get_instance_private (frame_clock);

  g_return_if_fail (!priv->listener.iface);
  g_return_if_fail (!priv->listener.user_data);

  priv->listener.iface = iface;
  priv->listener.user_data = user_data;
}
