/*
 * Copyright (C) 2017 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include <wayland-server.h>

#include "meta/window.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_TEXT_INPUT (meta_wayland_text_input_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandTextInput,
                      meta_wayland_text_input,
                      META, WAYLAND_TEXT_INPUT,
                      GObject);

MetaWaylandTextInput * meta_wayland_text_input_new (MetaWaylandSeat *seat);
void meta_wayland_text_input_destroy (MetaWaylandTextInput *text_input);

gboolean meta_wayland_text_input_init (MetaWaylandCompositor *compositor);

void meta_wayland_text_input_set_focus (MetaWaylandTextInput *text_input,
					MetaWaylandSurface   *surface);

gboolean meta_wayland_text_input_update (MetaWaylandTextInput *text_input,
                                         const ClutterEvent   *event);

gboolean meta_wayland_text_input_handle_event (MetaWaylandTextInput *text_input,
                                               const ClutterEvent   *event);

void meta_wayland_text_input_cache_event (MetaWaylandTextInput *text_input,
                                          const ClutterEvent   *event,
                                          uint32_t              serial);
