/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2024 Intel Corporation
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
 * Written by:
 *     Naveen Kumar <naveen1.kumar@intel.com>
 */

/**
 * MetaWaylandTearingControl
 *
 * Handles passing Tearing Control in Wayland
 *
 * The MetaWaylandTearingControl namespace adds core support for tearing control
 * that are passed through from clients in Wayland (e.g.
 * using the tearing_control_staging_v1 protocol).
 */

#pragma once

#include <glib.h>

#include "wayland/meta-wayland-types.h"

void meta_wayland_tearing_controller_init (MetaWaylandCompositor *compositor);
