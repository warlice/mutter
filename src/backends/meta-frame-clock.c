/*
 * Copyright (C) 2019-2021 Red Hat Inc.
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

#include "config.h"

#include "backends/meta-frame-clock.h"

#include "clutter/clutter-mutter.h"

/* An estimate queue holds several int64_t values. Adding a new value to the
 * queue overwrites the oldest value.
 */
#define ESTIMATE_QUEUE_LENGTH 16

typedef struct _EstimateQueue
{
  int64_t values[ESTIMATE_QUEUE_LENGTH];
  int next_index;
} EstimateQueue;

#define SYNC_DELAY_FALLBACK_FRACTION 0.875

typedef struct _MetaFrameClockSource
{
  GSource source;

  MetaFrameClock *frame_clock;
} MetaFrameClockSource;

typedef enum _MetaFrameClockState
{
  META_FRAME_CLOCK_STATE_INIT,
  META_FRAME_CLOCK_STATE_IDLE,
  META_FRAME_CLOCK_STATE_SCHEDULED,
  META_FRAME_CLOCK_STATE_DISPATCHING,
  META_FRAME_CLOCK_STATE_PENDING_PRESENTED,
} MetaFrameClockState;

struct _MetaFrameClock
{
  ClutterFrameClock parent;
};

typedef struct _MetaFrameClockPrivate
{
  float refresh_rate;
  int64_t refresh_interval_us;

  GSource *source;

  MetaFrameClockState state;
  int64_t last_dispatch_time_us;
  int64_t last_dispatch_lateness_us;
  int64_t last_presentation_time_us;

  gboolean is_next_presentation_time_valid;
  int64_t next_presentation_time_us;

  /* Buffer must be submitted to KMS and GPU rendering must be finished
   * this amount of time before the next presentation time.
   */
  int64_t vblank_duration_us;
  /* Last KMS buffer submission time. */
  int64_t last_flip_time_us;

  /* Last few durations between dispatch start and buffer swap. */
  EstimateQueue dispatch_to_swap_us;
  /* Last few durations between buffer swap and GPU rendering finish. */
  EstimateQueue swap_to_rendering_done_us;
  /* Last few durations between buffer swap and KMS submission. */
  EstimateQueue swap_to_flip_us;
  /* If we got new measurements last frame. */
  gboolean got_measurements_last_frame;
} MetaFrameClockPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaFrameClock, meta_frame_clock, CLUTTER_TYPE_FRAME_CLOCK)

static int64_t
compute_max_render_time_us (MetaFrameClock *frame_clock)
{
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);
  unsigned int clutter_paint_debug_flags;
  int64_t refresh_interval_us;
  int64_t max_dispatch_to_swap_us = 0;
  int64_t max_swap_to_rendering_done_us = 0;
  int64_t max_swap_to_flip_us = 0;
  int64_t max_render_time_us;
  int i;

  refresh_interval_us =
    (int64_t) (0.5 + G_USEC_PER_SEC / priv->refresh_rate);

  meta_get_clutter_debug_flags (NULL, &clutter_paint_debug_flags, NULL);
  if (!priv->got_measurements_last_frame ||
      G_UNLIKELY (clutter_paint_debug_flags &
                  CLUTTER_DEBUG_DISABLE_DYNAMIC_MAX_RENDER_TIME))
    return refresh_interval_us * SYNC_DELAY_FALLBACK_FRACTION;

  for (i = 0; i < ESTIMATE_QUEUE_LENGTH; ++i)
    {
      max_dispatch_to_swap_us =
        MAX (max_dispatch_to_swap_us,
             priv->dispatch_to_swap_us.values[i]);
      max_swap_to_rendering_done_us =
        MAX (max_swap_to_rendering_done_us,
             priv->swap_to_rendering_done_us.values[i]);
      max_swap_to_flip_us =
        MAX (max_swap_to_flip_us,
             priv->swap_to_flip_us.values[i]);
    }

  /* Max render time shows how early the frame clock needs to be dispatched
   * to make it to the predicted next presentation time. It is composed of:
   * - An estimate of duration from dispatch start to buffer swap.
   * - Maximum between estimates of duration from buffer swap to GPU rendering
   *   finish and duration from buffer swap to buffer submission to KMS. This
   *   is because both of these things need to happen before the vblank, and
   *   they are done in parallel.
   * - Duration of the vblank.
   * - A constant to account for variations in the above estimates.
   */
  max_render_time_us =
    max_dispatch_to_swap_us +
    MAX (max_swap_to_rendering_done_us, max_swap_to_flip_us) +
    priv->vblank_duration_us +
    max_render_time_constant_us;

  max_render_time_us = CLAMP (max_render_time_us, 0, refresh_interval_us);

  return max_render_time_us;
}

static void
calculate_next_update_time_us (MetaFrameClock *frame_clock,
                               int64_t        *out_next_update_time_us,
                               int64_t        *out_next_presentation_time_us)
{
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);
  int64_t last_presentation_time_us;
  int64_t now_us;
  int64_t refresh_interval_us;
  int64_t min_render_time_allowed_us;
  int64_t max_render_time_allowed_us;
  int64_t next_presentation_time_us;
  int64_t next_update_time_us;

  now_us = g_get_monotonic_time ();

  refresh_interval_us = priv->refresh_interval_us;

  if (priv->last_presentation_time_us == 0)
    {
      *out_next_update_time_us =
        priv->last_dispatch_time_us ?
        ((priv->last_dispatch_time_us -
          priv->last_dispatch_lateness_us) + refresh_interval_us) :
        now_us;

      *out_next_presentation_time_us = 0;
      return;
    }

  min_render_time_allowed_us = refresh_interval_us / 2;
  max_render_time_allowed_us = compute_max_render_time_us (frame_clock);

  if (min_render_time_allowed_us > max_render_time_allowed_us)
    min_render_time_allowed_us = max_render_time_allowed_us;

  /*
   * The common case is that the next presentation happens 1 refresh interval
   * after the last presentation:
   *
   *        last_presentation_time_us
   *       /       next_presentation_time_us
   *      /       /
   *     /       /
   * |--|--o----|-------|--> presentation times
   * |  |  \    |
   * |  |   now_us
   * |  \______/
   * | refresh_interval_us
   * |
   * 0
   *
   */
  last_presentation_time_us = priv->last_presentation_time_us;
  next_presentation_time_us = last_presentation_time_us + refresh_interval_us;

  /*
   * However, the last presentation could have happened more than a frame ago.
   * For example, due to idling (nothing on screen changed, so no need to
   * redraw) or due to frames missing deadlines (GPU busy with heavy rendering).
   * The following code adjusts next_presentation_time_us to be in the future,
   * but still aligned to display presentation times. Instead of
   * next presentation = last presentation + 1 * refresh interval, it will be
   * next presentation = last presentation + N * refresh interval.
   */
  if (next_presentation_time_us < now_us)
    {
      int64_t presentation_phase_us;
      int64_t current_phase_us;
      int64_t current_refresh_interval_start_us;

      /*
       * Let's say we're just past next_presentation_time_us.
       *
       * First, we compute presentation_phase_us. Real presentation times don't
       * have to be exact multiples of refresh_interval_us and
       * presentation_phase_us represents this difference. Next, we compute
       * current phase and the refresh interval start corresponding to now_us.
       * Finally, add presentation_phase_us and a refresh interval to get the
       * next presentation after now_us.
       *
       *        last_presentation_time_us
       *       /       next_presentation_time_us
       *      /       /   now_us
       *     /       /   /   new next_presentation_time_us
       * |--|-------|---o---|-------|--> presentation times
       * |        __|
       * |       |presentation_phase_us
       * |       |
       * |       |     now_us - presentation_phase_us
       * |       |    /
       * |-------|---o---|-------|-----> integer multiples of refresh_interval_us
       * |       \__/
       * |       |current_phase_us
       * |       \
       * |        current_refresh_interval_start_us
       * 0
       *
       */

      presentation_phase_us = last_presentation_time_us % refresh_interval_us;
      current_phase_us = (now_us - presentation_phase_us) % refresh_interval_us;
      current_refresh_interval_start_us =
        now_us - presentation_phase_us - current_phase_us;

      next_presentation_time_us =
        current_refresh_interval_start_us +
        presentation_phase_us +
        refresh_interval_us;
    }

  if (priv->is_next_presentation_time_valid)
    {
      int64_t last_next_presentation_time_us;
      int64_t time_since_last_next_presentation_time_us;

      /*
       * Skip one interval if we got an early presented event.
       *
       *        last frame this was last_presentation_time
       *       /       frame_clock::next_presentation_time_us
       *      /       /
       * |---|-o-----|-x----->
       *       |       \
       *       \        next_presentation_time_us is thus right after the last one
       *        but got an unexpected early presentation
       *             \_/
       *             time_since_last_next_presentation_time_us
       *
       */
      last_next_presentation_time_us = priv->next_presentation_time_us;
      time_since_last_next_presentation_time_us =
        next_presentation_time_us - last_next_presentation_time_us;
      if (time_since_last_next_presentation_time_us > 0 &&
          time_since_last_next_presentation_time_us < (refresh_interval_us / 2))
        {
          next_presentation_time_us =
            priv->next_presentation_time_us + refresh_interval_us;
        }
    }

  while (next_presentation_time_us < now_us + min_render_time_allowed_us)
    next_presentation_time_us += refresh_interval_us;

  next_update_time_us = next_presentation_time_us - max_render_time_allowed_us;

  *out_next_update_time_us = next_update_time_us;
  *out_next_presentation_time_us = next_presentation_time_us;
}

static void
estimate_queue_add_value (EstimateQueue *queue,
                          int64_t        value)
{
  queue->values[queue->next_index] = value;
  queue->next_index = (queue->next_index + 1) % ESTIMATE_QUEUE_LENGTH;
}

static void
meta_frame_clock_set_refresh_rate (MetaFrameClock *frame_clock,
                                   float           refresh_rate)
{
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  priv->refresh_rate = refresh_rate;
  priv->refresh_interval_us =
    (int64_t) (0.5 + G_USEC_PER_SEC / refresh_rate);
}

static void
meta_frame_clock_notify_presented (ClutterFrameClock *clock,
                                   ClutterFrameInfo  *frame_info)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  COGL_TRACE_BEGIN_SCOPED (ClutterFrameClockNotifyPresented,
                           "Frame Clock (presented)");

#ifdef COGL_HAS_TRACING
  if (G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      int64_t current_time_us;
      g_autoptr (GString) description = NULL;

      current_time_us = g_get_monotonic_time ();
      description = g_string_new (NULL);

      if (frame_info->presentation_time != 0)
        {
          if (frame_info->presentation_time <= current_time_us)
            {
              g_string_append_printf (description,
                                      "presentation was %ld µs earlier",
                                      current_time_us - frame_info->presentation_time);
            }
          else
            {
              g_string_append_printf (description,
                                      "presentation will be %ld µs later",
                                      frame_info->presentation_time - current_time_us);
            }
        }

      if (frame_info->gpu_rendering_duration_ns != 0)
        {
          if (description->len > 0)
            g_string_append (description, ", ");

          g_string_append_printf (description,
                                  "buffer swap to GPU done: %ld µs",
                                  ns2us (frame_info->gpu_rendering_duration_ns));
        }

      COGL_TRACE_DESCRIBE (ClutterFrameClockNotifyPresented, description->str);
    }
#endif

  if (frame_info->presentation_time > 0)
    priv->last_presentation_time_us = frame_info->presentation_time;

  priv->last_presentation_time_us = frame_info->presentation_time;

  priv->got_measurements_last_frame = FALSE;

  if (frame_info->cpu_time_before_buffer_swap_us != 0 &&
      frame_info->gpu_rendering_duration_ns != 0)
    {
      int64_t dispatch_to_swap_us, swap_to_rendering_done_us, swap_to_flip_us;

      dispatch_to_swap_us =
        frame_info->cpu_time_before_buffer_swap_us -
        priv->last_dispatch_time_us;
      swap_to_rendering_done_us =
        frame_info->gpu_rendering_duration_ns / 1000;
      swap_to_flip_us =
        priv->last_flip_time_us -
        frame_info->cpu_time_before_buffer_swap_us;

      CLUTTER_NOTE (FRAME_TIMINGS,
                    "dispatch2swap %ld µs, swap2render %ld µs, swap2flip %ld µs",
                    dispatch_to_swap_us,
                    swap_to_rendering_done_us,
                    swap_to_flip_us);

      estimate_queue_add_value (&priv->dispatch_to_swap_us,
                                dispatch_to_swap_us);
      estimate_queue_add_value (&priv->swap_to_rendering_done_us,
                                swap_to_rendering_done_us);
      estimate_queue_add_value (&priv->swap_to_flip_us,
                                swap_to_flip_us);

      priv->got_measurements_last_frame = TRUE;
    }

  if (frame_info->refresh_rate > 1.0)
    meta_frame_clock_set_refresh_rate (frame_clock, frame_info->refresh_rate);

  switch (priv->state)
    {
    case META_FRAME_CLOCK_STATE_INIT:
    case META_FRAME_CLOCK_STATE_IDLE:
    case META_FRAME_CLOCK_STATE_SCHEDULED:
      g_warn_if_reached ();
      break;
    case META_FRAME_CLOCK_STATE_DISPATCHING:
    case META_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      priv->state = META_FRAME_CLOCK_STATE_IDLE;
      clutter_frame_clock_maybe_reschedule_update (clock);
      break;
    }
}

static void
meta_frame_clock_notify_ready (ClutterFrameClock *clock)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  COGL_TRACE_BEGIN_SCOPED (NotifyReady, "Frame Clock (ready)");

  switch (priv->state)
    {
    case META_FRAME_CLOCK_STATE_INIT:
    case META_FRAME_CLOCK_STATE_IDLE:
    case META_FRAME_CLOCK_STATE_SCHEDULED:
      g_warn_if_reached ();
      break;
    case META_FRAME_CLOCK_STATE_DISPATCHING:
    case META_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      priv->state = META_FRAME_CLOCK_STATE_IDLE;
      clutter_frame_clock_maybe_reschedule_update (clock);
      break;
    }
}

static void
meta_frame_clock_inhibited (ClutterFrameClock *clock)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  switch (priv->state)
    {
    case META_FRAME_CLOCK_STATE_INIT:
    case META_FRAME_CLOCK_STATE_IDLE:
      break;
    case META_FRAME_CLOCK_STATE_SCHEDULED:
      clutter_frame_clock_reschedule (clock);
      priv->state = META_FRAME_CLOCK_STATE_IDLE;
      break;
    case META_FRAME_CLOCK_STATE_DISPATCHING:
    case META_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      break;
    }

  g_source_set_ready_time (priv->source, -1);
}

static void
meta_frame_clock_schedule_update_now (ClutterFrameClock *clock)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);
  int64_t next_update_time_us = -1;

  switch (priv->state)
    {
    case META_FRAME_CLOCK_STATE_INIT:
    case META_FRAME_CLOCK_STATE_IDLE:
      next_update_time_us = g_get_monotonic_time ();
      break;
    case META_FRAME_CLOCK_STATE_SCHEDULED:
      return;
    case META_FRAME_CLOCK_STATE_DISPATCHING:
    case META_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      clutter_frame_clock_reschedule_now (clock);
      return;
    }

  g_warn_if_fail (next_update_time_us != -1);

  g_source_set_ready_time (priv->source, next_update_time_us);
  priv->state = META_FRAME_CLOCK_STATE_SCHEDULED;
  priv->is_next_presentation_time_valid = FALSE;
}

static void
meta_frame_clock_schedule_update (ClutterFrameClock *clock)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);
  int64_t next_update_time_us = -1;

  switch (priv->state)
    {
    case META_FRAME_CLOCK_STATE_INIT:
      next_update_time_us = g_get_monotonic_time ();
      break;
    case META_FRAME_CLOCK_STATE_IDLE:
      calculate_next_update_time_us (frame_clock,
                                     &next_update_time_us,
                                     &priv->next_presentation_time_us);
      priv->is_next_presentation_time_valid =
        (priv->next_presentation_time_us != 0);
      break;
    case META_FRAME_CLOCK_STATE_SCHEDULED:
      return;
    case META_FRAME_CLOCK_STATE_DISPATCHING:
    case META_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      clutter_frame_clock_reschedule (clock);
      return;
    }

  g_warn_if_fail (next_update_time_us != -1);

  g_source_set_ready_time (priv->source, next_update_time_us);
  priv->state = META_FRAME_CLOCK_STATE_SCHEDULED;
}

static int64_t
meta_frame_clock_pre_dispatch (ClutterFrameClock *clock,
                               int64_t            time_us)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);
  int64_t ideal_dispatch_time_us;
  int64_t lateness_us;

  ideal_dispatch_time_us = (priv->last_dispatch_time_us -
                            priv->last_dispatch_lateness_us) +
                           priv->refresh_interval_us;

  lateness_us = time_us - ideal_dispatch_time_us;
  if (lateness_us < 0 || lateness_us >= priv->refresh_interval_us)
    priv->last_dispatch_lateness_us = 0;
  else
    priv->last_dispatch_lateness_us = lateness_us;

  priv->last_dispatch_time_us = time_us;
  g_source_set_ready_time (priv->source, -1);

  priv->state = META_FRAME_CLOCK_STATE_DISPATCHING;

  if (priv->is_next_presentation_time_valid)
    return priv->next_presentation_time_us;
  else
    return time_us;
}

static void
meta_frame_clock_post_dispatch (ClutterFrameClock  *clock,
                                ClutterFrameResult  result)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  switch (priv->state)
    {
    case META_FRAME_CLOCK_STATE_INIT:
    case META_FRAME_CLOCK_STATE_PENDING_PRESENTED:
      g_warn_if_reached ();
      break;
    case META_FRAME_CLOCK_STATE_IDLE:
    case META_FRAME_CLOCK_STATE_SCHEDULED:
      break;
    case META_FRAME_CLOCK_STATE_DISPATCHING:
      switch (result)
        {
        case CLUTTER_FRAME_RESULT_PENDING_PRESENTED:
          priv->state = META_FRAME_CLOCK_STATE_PENDING_PRESENTED;
          break;
        case CLUTTER_FRAME_RESULT_IDLE:
          priv->state = META_FRAME_CLOCK_STATE_IDLE;
          clutter_frame_clock_maybe_reschedule_update (clock);
          break;
        }
      break;
    }
}

static void
meta_frame_clock_record_flip_time (ClutterFrameClock *clock,
                                   int64_t            flip_time_us)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  priv->last_flip_time_us = flip_time_us;
}

static int
meta_frame_clock_get_priority (ClutterFrameClock *clock)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  return (int) roundf (priv->refresh_rate * 1000);
}

static gboolean
frame_clock_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  MetaFrameClockSource *clock_source = (MetaFrameClockSource *) source;
  MetaFrameClock *frame_clock = clock_source->frame_clock;
  int64_t dispatch_time_us;

  COGL_TRACE_BEGIN_SCOPED (SourceDispatch, "Frame Clock (source dispatch)");

  dispatch_time_us = g_source_get_time (source);

  if (G_UNLIKELY (cogl_is_tracing_enabled ()))
    {
      int64_t ready_time_us;
      g_autofree char *description = NULL;

      ready_time_us = g_source_get_ready_time (source);
      description = g_strdup_printf ("dispatched %ld µs late",
                                     dispatch_time_us - ready_time_us);
      COGL_TRACE_DESCRIBE (SourceDispatch, description);
    }

  clutter_frame_clock_dispatch (CLUTTER_FRAME_CLOCK (frame_clock),
                                dispatch_time_us);

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs frame_clock_source_funcs = {
  .dispatch = frame_clock_source_dispatch,
};

static void
init_frame_clock_source (MetaFrameClock *clock)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (clock);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);
  GSource *source;
  MetaFrameClockSource *clock_source;
  g_autofree char *name = NULL;

  source = g_source_new (&frame_clock_source_funcs,
                         sizeof (MetaFrameClockSource));
  clock_source = (MetaFrameClockSource *) source;

  name = g_strdup_printf ("[mutter] Clutter frame clock (%p)", frame_clock);
  g_source_set_name (source, name);
  g_source_set_priority (source, CLUTTER_PRIORITY_REDRAW);
  g_source_set_can_recurse (source, FALSE);
  clock_source->frame_clock = frame_clock;

  priv->source = source;
  g_source_attach (source, NULL);
  g_source_unref (source);
}

static void
meta_frame_clock_dispose (GObject *object)
{
  MetaFrameClock *frame_clock = META_FRAME_CLOCK (object);
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  g_clear_pointer (&priv->source, g_source_destroy);

  G_OBJECT_CLASS (meta_frame_clock_parent_class)->dispose (object);
}

static void
meta_frame_clock_class_init (MetaFrameClockClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterFrameClockClass *frame_clock_class = CLUTTER_FRAME_CLOCK_CLASS (klass);

  object_class->dispose = meta_frame_clock_dispose;

  frame_clock_class->notify_presented = meta_frame_clock_notify_presented;
  frame_clock_class->notify_ready = meta_frame_clock_notify_ready;
  frame_clock_class->inhibited = meta_frame_clock_inhibited;
  frame_clock_class->schedule_update_now = meta_frame_clock_schedule_update_now;
  frame_clock_class->schedule_update = meta_frame_clock_schedule_update;
  frame_clock_class->pre_dispatch = meta_frame_clock_pre_dispatch;
  frame_clock_class->post_dispatch = meta_frame_clock_post_dispatch;
  frame_clock_class->record_flip_time = meta_frame_clock_record_flip_time;
  frame_clock_class->get_priority = meta_frame_clock_get_priority;
}

static void
meta_frame_clock_init (MetaFrameClock *frame_clock)
{
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  priv->state = META_FRAME_CLOCK_STATE_INIT;
}

float
meta_frame_clock_get_refresh_rate (MetaFrameClock *frame_clock)
{
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);

  return priv->refresh_rate;
}

GString *
meta_frame_clock_get_max_render_time_debug_info (MetaFrameClock *frame_clock)
{
  MetaFrameClockPrivate *priv =
    meta_frame_clock_get_instance_private (frame_clock);
  int64_t max_dispatch_to_swap_us = 0;
  int64_t max_swap_to_rendering_done_us = 0;
  int64_t max_swap_to_flip_us = 0;
  int i;
  GString *string;

  string = g_string_new (NULL);
  g_string_append_printf (string, "Max render time: %ld µs",
                          compute_max_render_time_us (frame_clock));

  if (priv->got_measurements_last_frame)
    g_string_append_printf (string, " =");
  else
    g_string_append_printf (string, " (no measurements last frame)");

  for (i = 0; i < ESTIMATE_QUEUE_LENGTH; ++i)
    {
      max_dispatch_to_swap_us =
        MAX (max_dispatch_to_swap_us,
             priv->dispatch_to_swap_us.values[i]);
      max_swap_to_rendering_done_us =
        MAX (max_swap_to_rendering_done_us,
             priv->swap_to_rendering_done_us.values[i]);
      max_swap_to_flip_us =
        MAX (max_swap_to_flip_us,
             priv->swap_to_flip_us.values[i]);
    }

  g_string_append_printf (string, "\nVblank duration: %ld µs +",
                          priv->vblank_duration_us);
  g_string_append_printf (string, "\nDispatch to swap: %ld µs +",
                          max_dispatch_to_swap_us);
  g_string_append_printf (string, "\nmax(Swap to rendering done: %ld µs,",
                          max_swap_to_rendering_done_us);
  g_string_append_printf (string, "\nSwap to flip: %ld µs) +",
                          max_swap_to_flip_us);
  g_string_append_printf (string, "\nConstant: %d µs",
                          max_render_time_constant_us);

  return string;
}

MetaFrameClock *
meta_frame_clock_new (float   refresh_rate,
                      int64_t vblank_duration_us)
{
  MetaFrameClockPrivate *priv;
  MetaFrameClock *frame_clock;

  g_assert_cmpfloat (refresh_rate, >, 0.0);

  frame_clock = g_object_new (META_TYPE_FRAME_CLOCK, NULL);
  priv = meta_frame_clock_get_instance_private (frame_clock);

  init_frame_clock_source (frame_clock);

  meta_frame_clock_set_refresh_rate (frame_clock, refresh_rate);
  priv->vblank_duration_us = vblank_duration_us;

  return frame_clock;
}
