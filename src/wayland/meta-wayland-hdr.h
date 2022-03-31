/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2023 Intel Corporation.
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
 * along with this program;
 *
 * Authors:
 *   Naveen Kumar <naveen1.kumar@intel.com>
 *
 */

#ifndef META_WAYLAND_HDR_H
#define META_WAYLAND_HDR_H

#include <glib.h>
#include <glib-object.h>

#include "cogl/cogl.h"
#include "wayland/meta-wayland-types.h"

gboolean meta_wayland_hdr_init (MetaWaylandCompositor *compositor);

#endif /* META_WAYLAND_HDR_H */

