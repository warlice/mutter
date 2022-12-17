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

#ifndef META_FRAME_CLOCK_H
#define META_FRAME_CLOCK_H

#include <glib.h>
#include <glib-object.h>
#include <stdint.h>

#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "core/util-private.h"

#define META_TYPE_FRAME_CLOCK (meta_frame_clock_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaFrameClock, meta_frame_clock,
                      META, FRAME_CLOCK,
                      ClutterFrameClock)

META_EXPORT_TEST
MetaFrameClock * meta_frame_clock_new (float   refresh_rate,
                                       int64_t vblank_duration_us);

META_EXPORT_TEST
float meta_frame_clock_get_refresh_rate (MetaFrameClock *frame_clock);

GString * meta_frame_clock_get_max_render_time_debug_info (MetaFrameClock *frame_clock);

#endif /* META_FRAME_CLOCK_H */
