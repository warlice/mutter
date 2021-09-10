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

#ifndef MDK_PIPEWIRE_H
#define MDK_PIPEWIRE_H

#include <glib-object.h>

#include "mdk-types.h"

#define MDK_TYPE_PIPEWIRE (mdk_pipewire_get_type ())
G_DECLARE_FINAL_TYPE (MdkPipewire, mdk_pipewire,
                      MDK, PIPEWIRE,
                      GObject)

MdkPipewire * mdk_pipewire_new (MdkContext  *context,
                                GError     **error);

struct pw_core * mdk_pipewire_get_core (MdkPipewire *pipewire);

#endif /* MDK_PIPEWIRE_H */
