/*
 * Copyright (C) 2022 Jonas Dreßler <verdre@v0yd.nl>
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
 * ClutterPanGesture:
 *
 * A #ClutterGesture subclass for recognizing pan gestures
 */

#include "clutter/clutter-pan-gesture.h"

#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-private.h"

#define DEFAULT_BEGIN_THRESHOLD_PX 16

#define EVENT_HISTORY_DURATION_MS 150
#define EVENT_HISTORY_MIN_STORE_INTERVAL_MS 1
#define EVENT_HISTORY_MAX_LENGTH (EVENT_HISTORY_DURATION_MS / EVENT_HISTORY_MIN_STORE_INTERVAL_MS)

typedef struct
{
  graphene_vec2_t delta;
  uint32_t time;
} HistoryEntry;

typedef struct _ClutterPanGesture ClutterPanGesture;
typedef struct _ClutterPanGesturePrivate ClutterPanGesturePrivate;

struct _ClutterPanGesture
{
  ClutterGesture parent;

  ClutterPanGesturePrivate *priv;
};

struct _ClutterPanGesturePrivate
{
  int begin_threshold;
  gboolean threshold_reached;

  GArray *event_history;
  unsigned int event_history_begin_index;
  uint32_t latest_event_time;

  graphene_point_t start_point;
  graphene_vec2_t total_delta;

  ClutterPanAxis pan_axis;

  unsigned int min_n_points;
  unsigned int max_n_points;

  unsigned int use_point;
};

enum
{
  PROP_0,

  PROP_BEGIN_THRESHOLD,
  PROP_PAN_AXIS,
  PROP_MIN_N_POINTS,
  PROP_MAX_N_POINTS,

  PROP_LAST
};

enum
{
  PAN_UPDATE,

  LAST_SIGNAL
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };
static unsigned int obj_signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (ClutterPanGesture, clutter_pan_gesture, CLUTTER_TYPE_GESTURE)

static void
add_delta_to_event_history (ClutterPanGesture     *self,
                            const graphene_vec2_t *delta,
                            uint32_t               time)
{
  ClutterPanGesturePrivate *priv =
    clutter_pan_gesture_get_instance_private (self);
  HistoryEntry *last_history_entry, *history_entry;

  last_history_entry = priv->event_history->len == 0
    ? NULL
    : &g_array_index (priv->event_history,
                      HistoryEntry,
                      (priv->event_history_begin_index - 1) % EVENT_HISTORY_MAX_LENGTH);

  if (last_history_entry &&
      last_history_entry->time > (time - EVENT_HISTORY_MIN_STORE_INTERVAL_MS))
    return;

  if (priv->event_history->len < EVENT_HISTORY_MAX_LENGTH)
    g_array_set_size (priv->event_history, priv->event_history->len + 1);

  history_entry =
    &g_array_index (priv->event_history, HistoryEntry, priv->event_history_begin_index);

  history_entry->delta = *delta;
  history_entry->time = time;

  priv->event_history_begin_index =
    (priv->event_history_begin_index + 1) % EVENT_HISTORY_MAX_LENGTH;
}

static void
calculate_velocity (ClutterPanGesture *self,
                    graphene_vec2_t   *velocity)
{
  ClutterPanGesturePrivate *priv =
    clutter_pan_gesture_get_instance_private (self);
  unsigned int i, j;
  uint32_t first_time = 0;
  uint32_t last_time = 0;
  uint32_t time_delta;
  graphene_vec2_t accumulated_deltas = { 0 };

  j = priv->event_history_begin_index;

  for (i = 0; i < priv->event_history->len; i++)
    {
      HistoryEntry *history_entry;

      if (j == priv->event_history->len)
        j = 0;

      history_entry = &g_array_index (priv->event_history, HistoryEntry, j);

      if (history_entry->time >= priv->latest_event_time - EVENT_HISTORY_DURATION_MS)
        {
          if (first_time == 0)
            first_time = history_entry->time;

          graphene_vec2_add (&accumulated_deltas, &history_entry->delta, &accumulated_deltas);

          last_time = history_entry->time;
        }

      j++;
    }

  if (first_time == last_time)
    {
      graphene_vec2_init (velocity, 0, 0);
      return;
    }

  time_delta = last_time - first_time;
  graphene_vec2_init (velocity,
                      graphene_vec2_get_x (&accumulated_deltas) / time_delta,
                      graphene_vec2_get_y (&accumulated_deltas) / time_delta);
}

static void
get_centroid_from_points (ClutterPanGesture *self,
                          unsigned int      *points,
                          unsigned int       n_points,
                          graphene_point_t  *centroid)
{
  unsigned int i;
  float accu_x = 0;
  float accu_y = 0;

  for (i = 0; i < n_points; i++)
    {
      graphene_point_t coords;

      clutter_gesture_get_point_begin_coords_abs (CLUTTER_GESTURE (self),
                                                  points[i], &coords);

      accu_x += coords.x;
      accu_y += coords.y;
    }

  centroid->x = accu_x / n_points;
  centroid->y = accu_y / n_points;
}

static void
get_delta_from_points (ClutterPanGesture *self,
                       unsigned int      *points,
                       unsigned int       n_points,
                       graphene_vec2_t   *delta)
{
  graphene_vec2_t biggest_pos_delta, biggest_neg_delta;
  unsigned int i;

  graphene_vec2_init (&biggest_pos_delta, 0, 0);
  graphene_vec2_init (&biggest_neg_delta, 0, 0);

  for (i = 0; i < n_points; i++)
    {
      graphene_point_t latest_coords, previous_coords;
      float point_d_x, point_d_y;

      clutter_gesture_get_point_coords_abs (CLUTTER_GESTURE (self),
                                            points[i], &latest_coords);
      clutter_gesture_get_point_previous_coords_abs (CLUTTER_GESTURE (self),
                                                     points[i], &previous_coords);

      point_d_x = latest_coords.x - previous_coords.x;
      point_d_y = latest_coords.y - previous_coords.y;

      if (point_d_x > 0)
        {
          /* meh, graphene API is quite annoying here */
          graphene_vec2_init (&biggest_pos_delta,
                              MAX (point_d_x, graphene_vec2_get_x (&biggest_pos_delta)),
                              graphene_vec2_get_y (&biggest_pos_delta));
        }
      else
        {
          graphene_vec2_init (&biggest_neg_delta,
                              MIN (point_d_x, graphene_vec2_get_x (&biggest_neg_delta)),
                              graphene_vec2_get_y (&biggest_neg_delta));
        }

      if (point_d_y > 0)
        {
          graphene_vec2_init (&biggest_pos_delta,
                              graphene_vec2_get_x (&biggest_pos_delta),
                              MAX (point_d_y, graphene_vec2_get_y (&biggest_pos_delta)));

        }
      else
        {
          graphene_vec2_init (&biggest_neg_delta,
                              graphene_vec2_get_x (&biggest_neg_delta),
                              MIN (point_d_y, graphene_vec2_get_y (&biggest_neg_delta)));
        }
    }

  graphene_vec2_add (&biggest_pos_delta, &biggest_neg_delta, delta);
}

static gboolean
should_handle_sequence (ClutterGesture     *gesture,
                        const ClutterEvent *sequence_begin_event)
{
  ClutterEventType event_type = clutter_event_type (sequence_begin_event);

  if (event_type == CLUTTER_BUTTON_PRESS ||
      event_type == CLUTTER_TOUCH_BEGIN)
    return TRUE;

  return FALSE;
}

static void
point_began (ClutterGesture *gesture,
             unsigned int    sequence)
{
  ClutterPanGesture *self = CLUTTER_PAN_GESTURE (gesture);
  ClutterPanGesturePrivate *priv =
    clutter_pan_gesture_get_instance_private (self);
  unsigned int active_n_points = clutter_gesture_get_n_points (gesture);
  const ClutterEvent *event = clutter_gesture_get_point_event (gesture, sequence);

  if (active_n_points < priv->min_n_points)
    return;

  /* Most pan gestures will only want to use the primary button anyway, could
   * expose this as API later if necessary.
   */
  if (clutter_event_type (event) == CLUTTER_BUTTON_PRESS &&
      clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    {
      clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
      return;
    }

  if (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_POSSIBLE &&
      priv->max_n_points != 0 && active_n_points > priv->max_n_points)
    {
      clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
      return;
    }

  priv->threshold_reached = FALSE;
  priv->latest_event_time = clutter_event_get_time (event);

  if (priv->event_history->len == 0)
    add_delta_to_event_history (self, graphene_vec2_zero (), priv->latest_event_time);

  if (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_POSSIBLE &&
      (priv->begin_threshold == 0))
    {
      unsigned int *active_points = clutter_gesture_get_points (gesture, NULL);

      get_centroid_from_points (self, active_points, active_n_points, &priv->start_point);
      g_free (active_points);

      clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_RECOGNIZING);
    }

  priv->use_point = sequence;
}

static void
point_moved (ClutterGesture *gesture,
             unsigned int    sequence)
{
  ClutterPanGesture *self = CLUTTER_PAN_GESTURE (gesture);
  ClutterPanGesturePrivate *priv =
    clutter_pan_gesture_get_instance_private (self);
  unsigned int active_n_points = clutter_gesture_get_n_points (gesture);
  graphene_vec2_t delta;
  const ClutterEvent *event = clutter_gesture_get_point_event (gesture, sequence);
  float total_distance;

  /* We can make use of get_delta_from_points() with multiple points that happened
   * at the same time. This will allow handling multi-finger pans nicely.
   *
   * For now, we only look at the first point and ignore all other events that
   * happened at the same time though.
   */
  if (sequence != priv->use_point)
    return;

  priv->latest_event_time = clutter_event_get_time (event);

  get_delta_from_points (self, &sequence, 1, &delta);
  add_delta_to_event_history (self, &delta, priv->latest_event_time);

  graphene_vec2_add (&priv->total_delta, &delta, &priv->total_delta);
  total_distance = graphene_vec2_length (&priv->total_delta);

  if (!priv->threshold_reached &&
      ((priv->pan_axis == CLUTTER_PAN_AXIS_BOTH &&
        total_distance < priv->begin_threshold) ||
       (priv->pan_axis == CLUTTER_PAN_AXIS_X &&
        ABS (graphene_vec2_get_x (&priv->total_delta)) < priv->begin_threshold) ||
       (priv->pan_axis == CLUTTER_PAN_AXIS_Y &&
        ABS (graphene_vec2_get_y (&priv->total_delta)) < priv->begin_threshold)))
    return;

  priv->threshold_reached = TRUE;

  if (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_POSSIBLE &&
      active_n_points >= priv->min_n_points &&
      (priv->max_n_points == 0 || active_n_points <= priv->max_n_points))
    {
      get_centroid_from_points (self, &sequence, 1, &priv->start_point);
      clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_RECOGNIZING);
    }

  if (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_RECOGNIZING)
    g_signal_emit (self, obj_signals[PAN_UPDATE], 0);
}

static void
point_ended (ClutterGesture *gesture,
             unsigned int    sequence)
{
  ClutterPanGesture *self = CLUTTER_PAN_GESTURE (gesture);
  ClutterPanGesturePrivate *priv =
    clutter_pan_gesture_get_instance_private (self);
  unsigned int active_n_points = clutter_gesture_get_n_points (gesture);
  const ClutterEvent *event = clutter_gesture_get_point_event (gesture, sequence);

  if (active_n_points - 1 >= priv->min_n_points)
    {
      unsigned int *active_points = clutter_gesture_get_points (gesture, NULL);

      /* The point we were using ended but there's still enough points on screen
       * to allow the gesture to continue, so use another one to drive the gesture.
       */
      priv->use_point = active_points[0] != sequence
        ? active_points[0] : active_points[1];

      g_free (active_points);
      return;
    }

  priv->latest_event_time = clutter_event_get_time (event);

  if (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_RECOGNIZING)
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_COMPLETED);
  else
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
}

static void
state_changed (ClutterGesture      *gesture,
               ClutterGestureState  old_state,
               ClutterGestureState  new_state)
{
  ClutterPanGesture *self = CLUTTER_PAN_GESTURE (gesture);
  ClutterPanGesturePrivate *priv =
    clutter_pan_gesture_get_instance_private (self);

  if (new_state == CLUTTER_GESTURE_STATE_WAITING)
    {
      graphene_vec2_init (&priv->total_delta, 0, 0);
      priv->event_history_begin_index = 0;
      g_array_set_size (priv->event_history, 0);
    }
}

static void
clutter_pan_gesture_set_property (GObject      *gobject,
                                  unsigned int  prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  ClutterPanGesture *self = CLUTTER_PAN_GESTURE (gobject);

  switch (prop_id)
    {
    case PROP_BEGIN_THRESHOLD:
      clutter_pan_gesture_set_begin_threshold (self, g_value_get_uint (value));
      break;

    case PROP_PAN_AXIS:
      clutter_pan_gesture_set_pan_axis (self, g_value_get_enum (value));
      break;

    case PROP_MIN_N_POINTS:
      clutter_pan_gesture_set_min_n_points (self, g_value_get_uint (value));
      break;

    case PROP_MAX_N_POINTS:
      clutter_pan_gesture_set_max_n_points (self, g_value_get_uint (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
    }
}

static void
clutter_pan_gesture_get_property (GObject      *gobject,
                                  unsigned int  prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  ClutterPanGesture *self = CLUTTER_PAN_GESTURE (gobject);

  switch (prop_id)
    {
    case PROP_BEGIN_THRESHOLD:
      g_value_set_uint (value, clutter_pan_gesture_get_begin_threshold (self));
      break;

    case PROP_PAN_AXIS:
      g_value_set_enum (value, clutter_pan_gesture_get_pan_axis (self));
      break;

    case PROP_MIN_N_POINTS:
      g_value_set_uint (value, clutter_pan_gesture_get_min_n_points (self));
      break;

    case PROP_MAX_N_POINTS:
      g_value_set_uint (value, clutter_pan_gesture_get_max_n_points (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
    }
}

static void
clutter_pan_gesture_finalize (GObject *gobject)
{
  ClutterPanGesturePrivate *priv =
    clutter_pan_gesture_get_instance_private (CLUTTER_PAN_GESTURE (gobject));

  g_array_unref (priv->event_history);

  G_OBJECT_CLASS (clutter_pan_gesture_parent_class)->finalize (gobject);
}

static void
clutter_pan_gesture_class_init (ClutterPanGestureClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_CLASS (klass);

  gobject_class->set_property = clutter_pan_gesture_set_property;
  gobject_class->get_property = clutter_pan_gesture_get_property;
  gobject_class->finalize = clutter_pan_gesture_finalize;

  gesture_class->should_handle_sequence = should_handle_sequence;
  gesture_class->point_began = point_began;
  gesture_class->point_moved = point_moved;
  gesture_class->point_ended = point_ended;
  gesture_class->state_changed = state_changed;

  /**
   * ClutterPanGesture:begin-threshold:
   *
   * The threshold in pixels that has to be panned for the gesture to start.
   */
  obj_props[PROP_BEGIN_THRESHOLD] =
    g_param_spec_uint ("begin-threshold",
                       "begin-threshold",
                       "begin-threshold",
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterPanGesture:pan-axis:
   *
   * Constraints the pan gesture to the specified axis.
   */
  obj_props[PROP_PAN_AXIS] =
    g_param_spec_enum ("pan-axis",
                       "pan-axis",
                       "pan-axis",
                       CLUTTER_TYPE_PAN_AXIS,
                       CLUTTER_PAN_AXIS_BOTH,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterPanGesture:min-n-points:
   *
   * The minimum number of points for the gesture to start, defaults to 1.
   */
  obj_props[PROP_MIN_N_POINTS] =
    g_param_spec_uint ("min-n-points",
                       "min-n-points",
                       "min-n-points",
                       1, G_MAXUINT, 1,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterPanGesture:max-n-points:
   *
   * The maximum number of points to use for the pan. Set to 0 to allow
   * an unlimited number. Defaults to 0.
   */
  obj_props[PROP_MAX_N_POINTS] =
    g_param_spec_uint ("max-n-points",
                       "max-n-points",
                       "max-n-points",
                       0, G_MAXUINT, 1,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties  (gobject_class,
                                      PROP_LAST,
                                      obj_props);

  /**
   * ClutterPanGesture::pan-update:
   * @gesture: the #ClutterPanGesture that emitted the signal
   *
   * The ::pan-update signal is emitted when one or multiple points
   * of the pan have changed.
   */
  obj_signals[PAN_UPDATE] =
    g_signal_new ("pan-update",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0,
                  G_TYPE_NONE);
}

static void
clutter_pan_gesture_init (ClutterPanGesture *self)
{
  ClutterPanGesturePrivate *priv =
    clutter_pan_gesture_get_instance_private (self);

  priv->begin_threshold = DEFAULT_BEGIN_THRESHOLD_PX;
  priv->threshold_reached = FALSE;

  priv->event_history =
    g_array_sized_new (FALSE, TRUE, sizeof (HistoryEntry), EVENT_HISTORY_MAX_LENGTH);
  priv->event_history_begin_index = 0;

  priv->pan_axis = CLUTTER_PAN_AXIS_BOTH;
  priv->min_n_points = 1;
  priv->max_n_points = 0;

  priv->use_point = 0;
}

/**
 * clutter_pan_gesture_new:
 *
 * Creates a new #ClutterPanGesture instance
 *
 * Returns: the newly created #ClutterPanGesture
 */
ClutterAction *
clutter_pan_gesture_new (void)
{
  return g_object_new (CLUTTER_TYPE_PAN_GESTURE, NULL);
}

/**
 * clutter_pan_gesture_get_begin_threshold:
 * @self: a #ClutterPanGesture
 *
 * Gets the movement threshold in pixels that begins the pan gesture.
 *
 * Returns: The begin threshold in pixels
 */
unsigned int
clutter_pan_gesture_get_begin_threshold (ClutterPanGesture *self)
{
  ClutterPanGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PAN_GESTURE (self), 0);

  priv = clutter_pan_gesture_get_instance_private (self);

  return priv->begin_threshold;
}

/**
 * clutter_pan_gesture_set_begin_threshold:
 * @self: a #ClutterPanGesture
 * @begin_threshold: the threshold in pixels
 *
 * Sets the movement threshold in pixels to begin the pan gesture.
 */
void
clutter_pan_gesture_set_begin_threshold (ClutterPanGesture *self,
                                         unsigned int       begin_threshold)
{
  ClutterPanGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));

  priv = clutter_pan_gesture_get_instance_private (self);

  if (priv->begin_threshold == begin_threshold)
    return;

  priv->begin_threshold = begin_threshold;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_BEGIN_THRESHOLD]);

  if (clutter_gesture_get_state (CLUTTER_GESTURE (self)) == CLUTTER_GESTURE_STATE_POSSIBLE)
    {
      unsigned int active_n_points = 
        clutter_gesture_get_n_points (CLUTTER_GESTURE (self));

      if (active_n_points >= priv->min_n_points &&
          (priv->max_n_points == 0 || active_n_points <= priv->max_n_points))
        {
          if ((priv->pan_axis == CLUTTER_PAN_AXIS_BOTH &&
               graphene_vec2_length (&priv->total_delta) >= priv->begin_threshold) ||
              (priv->pan_axis == CLUTTER_PAN_AXIS_X &&
               ABS (graphene_vec2_get_x (&priv->total_delta)) >= priv->begin_threshold) ||
              (priv->pan_axis == CLUTTER_PAN_AXIS_Y &&
               ABS (graphene_vec2_get_y (&priv->total_delta)) >= priv->begin_threshold))
            clutter_gesture_set_state (CLUTTER_GESTURE (self), CLUTTER_GESTURE_STATE_RECOGNIZING);
        }
    }
}

/**
 * clutter_pan_gesture_get_pan_axis:
 * @self: a #ClutterPanGesture
 *
 * Retrieves the axis constraint set by clutter_pan_gesture_set_pan_axis().
 *
 * Returns: the axis constraint
 */
ClutterPanAxis
clutter_pan_gesture_get_pan_axis (ClutterPanGesture *self)
{
  ClutterPanGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PAN_GESTURE (self),
                        CLUTTER_PAN_AXIS_BOTH);

  priv = clutter_pan_gesture_get_instance_private (self);

  return priv->pan_axis;
}

/**
 * clutter_pan_gesture_set_pan_axis:
 * @self: a #ClutterPanGesture
 * @axis: the #ClutterPanAxis
 *
 * Restricts the pan gesture to a specific axis.
 */
void
clutter_pan_gesture_set_pan_axis (ClutterPanGesture *self,
                                  ClutterPanAxis     axis)
{
  ClutterPanGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));
  g_return_if_fail (axis != CLUTTER_PAN_AXIS_BOTH ||
                    axis != CLUTTER_PAN_AXIS_X ||
                    axis != CLUTTER_PAN_AXIS_Y);

  priv = clutter_pan_gesture_get_instance_private (self);

  if (priv->pan_axis == axis)
    return;

  priv->pan_axis = axis;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_PAN_AXIS]);
}

/**
 * clutter_pan_gesture_get_min_n_points:
 * @self: a #ClutterPanGesture
 *
 * Gets the minimum number of points set by
 * clutter_pan_gesture_set_min_n_points().
 *
 * Returns: the minimum number of points
 */
unsigned int
clutter_pan_gesture_get_min_n_points (ClutterPanGesture *self)
{
  ClutterPanGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PAN_GESTURE (self), 1);

  priv = clutter_pan_gesture_get_instance_private (self);

  return priv->min_n_points;
}

/**
 * clutter_pan_gesture_set_min_n_points:
 * @self: a #ClutterPanGesture
 * @min_n_points: the minimum number of points
 *
 * Sets the minimum number of points for the gesture to start.
 */
void
clutter_pan_gesture_set_min_n_points (ClutterPanGesture *self,
                                      unsigned int       min_n_points)
{
  ClutterPanGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));

  priv = clutter_pan_gesture_get_instance_private (self);

  g_return_if_fail (min_n_points >= 1 &&
                    (priv->max_n_points == 0 || min_n_points <= priv->max_n_points));

  if (priv->min_n_points == min_n_points)
    return;

  priv->min_n_points = min_n_points;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_MIN_N_POINTS]);
}

/**
 * clutter_pan_gesture_get_max_n_points:
 * @self: a #ClutterPanGesture
 *
 * Gets the maximum number of points set by
 * clutter_pan_gesture_set_max_n_points().
 *
 * Returns: the maximum number of points
 */
unsigned int
clutter_pan_gesture_get_max_n_points (ClutterPanGesture *self)
{
  ClutterPanGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PAN_GESTURE (self), 1);

  priv = clutter_pan_gesture_get_instance_private (self);

  return priv->max_n_points;
}

/**
 * clutter_pan_gesture_set_max_n_points:
 * @self: a #ClutterPanGesture
 * @max_n_points: the maximum number of points
 *
 * Sets the maximum number of points to use for the pan. Set to 0 to allow
 * an unlimited number.
 */
void
clutter_pan_gesture_set_max_n_points (ClutterPanGesture *self,
                                      unsigned int       max_n_points)
{
  ClutterPanGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));

  priv = clutter_pan_gesture_get_instance_private (self);

  g_return_if_fail (max_n_points == 0 || max_n_points >= priv->min_n_points);

  if (priv->max_n_points == max_n_points)
    return;

  priv->max_n_points = max_n_points;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_MAX_N_POINTS]);
}

/**
 * clutter_pan_gesture_get_begin_centroid:
 * @self: the #ClutterPanGesture
 * @centroid_out: (out caller-allocates): a #graphene_point_t
 *
 * Retrieves the begin centroid of @self.
 */
void
clutter_pan_gesture_get_begin_centroid (ClutterPanGesture *self,
                                        graphene_point_t  *centroid_out)
{
  ClutterPanGesturePrivate *priv;
  float x, y;
  ClutterActor *action_actor;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));
  g_return_if_fail (centroid_out != NULL);

  priv = clutter_pan_gesture_get_instance_private (self);

  x = priv->start_point.x;
  y = priv->start_point.y;

  action_actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  if (action_actor)
    clutter_actor_transform_stage_point (action_actor, x, y, &x, &y);

  centroid_out->x = x;
  centroid_out->y = y;
}

/**
 * clutter_pan_gesture_get_begin_centroid_abs:
 * @self: the #ClutterPanGesture
 * @centroid_out: (out caller-allocates): a #graphene_point_t
 *
 * Retrieves the begin centroid of @self in absolute coordinates.
 */
void
clutter_pan_gesture_get_begin_centroid_abs (ClutterPanGesture *self,
                                            graphene_point_t  *centroid_out)
{
  ClutterPanGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));
  g_return_if_fail (centroid_out != NULL);

  priv = clutter_pan_gesture_get_instance_private (self);

  centroid_out->x = priv->start_point.x;
  centroid_out->y = priv->start_point.y;
}

/**
 * clutter_pan_gesture_get_centroid:
 * @self: the #ClutterPanGesture
 * @centroid_out: (out): a #graphene_point_t
 *
 * Retrieves the current centroid of the points active on @self.
 *
 * Note that ClutterPanGesture tries to keep the centroid "stable" when points
 * are added or removed from the gesture: The centroid is driven from deltas
 * rather than the actual points on the screen.
 */
void
clutter_pan_gesture_get_centroid (ClutterPanGesture *self,
                                  graphene_point_t  *centroid_out)
{
  ClutterPanGesturePrivate *priv;
  float x, y;
  ClutterActor *action_actor;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));
  g_return_if_fail (centroid_out != NULL);

  priv = clutter_pan_gesture_get_instance_private (self);

  x = priv->start_point.x + graphene_vec2_get_x (&priv->total_delta);
  y = priv->start_point.y + graphene_vec2_get_y (&priv->total_delta);

  action_actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  if (action_actor)
    clutter_actor_transform_stage_point (action_actor, x, y, &x, &y);

  centroid_out->x = x;
  centroid_out->y = y;
}

/**
 * clutter_pan_gesture_get_centroid_abs:
 * @self: the #ClutterPanGesture
 * @centroid_out: (out): a #graphene_point_t
 *
 * Retrieves the current centroid of the points active on @self in
 * absolute coordinates.
 *
 * Note that ClutterPanGesture tries to keep the centroid "stable" when points
 * are added or removed from the gesture: The centroid is driven from deltas
 * rather than the actual points on the screen.
 */
void
clutter_pan_gesture_get_centroid_abs (ClutterPanGesture *self,
                                      graphene_point_t  *centroid_out)
{
  ClutterPanGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));
  g_return_if_fail (centroid_out != NULL);

  priv = clutter_pan_gesture_get_instance_private (self);

  centroid_out->x = priv->start_point.x + graphene_vec2_get_x (&priv->total_delta);
  centroid_out->y = priv->start_point.y + graphene_vec2_get_y (&priv->total_delta);
}

static void
get_actor_scale (ClutterActor    *actor,
                 graphene_vec2_t *scale_out)
{
  float actor_width, actor_height;
  graphene_rect_t transformed_extents;

  clutter_actor_get_size (actor, &actor_width, &actor_height);
  clutter_actor_get_transformed_extents (actor, &transformed_extents);

  graphene_vec2_init (scale_out,
                      actor_width / transformed_extents.size.width,
                      actor_height / transformed_extents.size.height);
}

/**
 * clutter_pan_gesture_get_velocity:
 * @self: the #ClutterPanGesture
 * @velocity_out: (out): a #graphene_vec2_t
 *
 * Retrieves the current velocity of the pan.
 */
void
clutter_pan_gesture_get_velocity (ClutterPanGesture *self,
                                  graphene_vec2_t   *velocity_out)
{
  ClutterPanGesturePrivate *priv;
  ClutterActor *action_actor;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));
  g_return_if_fail (velocity_out != NULL);

  priv = clutter_pan_gesture_get_instance_private (self);

  if (!priv->threshold_reached)
    {
      graphene_vec2_init (velocity_out, 0, 0);
      return;
    }

  calculate_velocity (self, velocity_out);

  action_actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  if (action_actor)
    {
      graphene_vec2_t actor_scale;

      get_actor_scale (action_actor, &actor_scale);
      graphene_vec2_multiply (velocity_out, &actor_scale, velocity_out);
    }
}

/**
 * clutter_pan_gesture_get_delta:
 * @self: the #ClutterPanGesture
 * @latest_delta_out: (out) (optional): the delta from the latest event
 * @total_delta_out: (out) (optional): the accumulated delta from all events
 *
 * Retrieves the delta between the current ::pan-update signal emission and the
 * one before as @latest_delta_out.
 *
 * The accumulated delta from all events (ie. the total delta that the pan has
 * been moved) is passed as @total_delta_out.
 *
 * This function is mostly meant to be called within ::pan-update signal handlers,
 * to get the delta that the pan has moved since the last ::pan-update emission.
 */
void
clutter_pan_gesture_get_delta (ClutterPanGesture *self,
                               graphene_vec2_t   *latest_delta_out,
                               graphene_vec2_t   *total_delta_out)
{
  ClutterPanGesturePrivate *priv;
  HistoryEntry *last_history_entry;
  ClutterActor *action_actor;

  g_return_if_fail (CLUTTER_IS_PAN_GESTURE (self));
  g_return_if_fail (latest_delta_out != NULL || total_delta_out != NULL);

  priv = clutter_pan_gesture_get_instance_private (self);

  last_history_entry = priv->event_history->len == 0
    ? NULL
    : &g_array_index (priv->event_history,
                      HistoryEntry,
                      (priv->event_history_begin_index - 1) % EVENT_HISTORY_MAX_LENGTH);

  if (!last_history_entry)
    {
      if (latest_delta_out)
        graphene_vec2_init (latest_delta_out, 0, 0);

      if (total_delta_out)
        graphene_vec2_init (total_delta_out, 0, 0);

      return;
    }

  if (latest_delta_out)
    *latest_delta_out = last_history_entry->delta;

  if (total_delta_out)
    *total_delta_out = priv->total_delta;

  action_actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  if (action_actor)
    {
      graphene_vec2_t actor_scale;

      get_actor_scale (action_actor, &actor_scale);

      if (latest_delta_out)
        graphene_vec2_multiply (latest_delta_out, &actor_scale, latest_delta_out);

      if (total_delta_out)
        graphene_vec2_multiply (total_delta_out, &actor_scale, total_delta_out);
    }
}
