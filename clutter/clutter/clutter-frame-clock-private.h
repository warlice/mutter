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

#ifndef CLUTTER_FRAME_CLOCK_PRIVATE_H
#define CLUTTER_FRAME_CLOCK_PRIVATE_H

#include "clutter/clutter-frame-clock.h"

struct _ClutterFrameClockClass
{
  GObjectClass parent_class;

  void (* notify_presented) (ClutterFrameClock *frame_clock,
                             ClutterFrameInfo  *frame_info);
  void (* notify_ready) (ClutterFrameClock *frame_clock);
  void (* inhibited) (ClutterFrameClock *frame_clock);
  void (* schedule_update_now) (ClutterFrameClock *frame_clock);
  void (* schedule_update) (ClutterFrameClock *frame_clock);
  int64_t (* pre_dispatch) (ClutterFrameClock *frame_clock,
                            int64_t            time_us);
  void (* post_dispatch) (ClutterFrameClock  *frame_clock,
                          ClutterFrameResult  result);
  void (* record_flip_time) (ClutterFrameClock *frame_clock,
                             int64_t            flip_time_us);
  int (* get_priority) (ClutterFrameClock *frame_clock);
};

#endif /* CLUTTER_FRAME_CLOCK_PRIVATE_H */
