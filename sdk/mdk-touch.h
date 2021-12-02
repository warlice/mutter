/*
 * Copyright (C) 2021 Red Hat Inc.
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

#ifndef MDK_TOUCH_H
#define MDK_TOUCH_H

#include <glib-object.h>

#include "mdk-types.h"

#define MDK_TYPE_TOUCH (mdk_touch_get_type ())
G_DECLARE_FINAL_TYPE (MdkTouch, mdk_touch, MDK, TOUCH, GObject)

MdkTouch * mdk_touch_new (MdkSession                  *session,
                          MdkDBusRemoteDesktopSession *session_proxy,
                          MdkMonitor                  *monitor);

void mdk_touch_release_all (MdkTouch *touch);

void mdk_touch_notify_down (MdkTouch *touch,
                            int       slot,
                            double    x,
                            double    y);


void mdk_touch_notify_motion (MdkTouch *touch,
                              int       slot,
                              double    x,
                              double    y);

void mdk_touch_notify_up (MdkTouch *touch,
                          int       slot);

#endif /* MDK_TOUCH_H */
