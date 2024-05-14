/*
 * Copyright (C) 2024 SUSE LLC
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Alynx Zhou <alynx.zhou@gmail.com>
 */

#pragma once

#include <wayland-server.h>

#include "meta/window.h"
#include "wayland/meta-wayland-types.h"

typedef struct _MetaWaylandTextInputV1 MetaWaylandTextInputV1;

MetaWaylandTextInputV1 * meta_wayland_text_input_v1_new (MetaWaylandSeat *seat);
void meta_wayland_text_input_v1_destroy (MetaWaylandTextInputV1 *text_input);

gboolean meta_wayland_text_input_v1_init (MetaWaylandCompositor *compositor);

gboolean meta_wayland_text_input_v1_update (MetaWaylandTextInputV1 *text_input,
                                            const ClutterEvent     *event);

gboolean meta_wayland_text_input_v1_handle_event (MetaWaylandTextInputV1 *text_input,
                                                  const ClutterEvent     *event);
