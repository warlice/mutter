/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * MetaPointerLockWayland:
 *
 * A #MetaPointerConstraint implementing pointer lock.
 *
 * A MetaPointerLockConstraint implements the client pointer constraint "pointer
 * lock": the cursor should not make any movement.
 */

#include "config.h"

#include "wayland/meta-pointer-lock-wayland.h"

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "compositor/meta-surface-actor-wayland.h"

struct _MetaPointerLockWayland
{
  GObject parent;
};

G_DEFINE_TYPE (MetaPointerLockWayland, meta_pointer_lock_wayland,
               META_TYPE_POINTER_CONFINEMENT_WAYLAND)

static MetaPointerConstraint *
meta_pointer_lock_wayland_create_constraint (MetaPointerConfinementWayland *confinement)
{
  MetaWaylandPointerConstraint *wayland_constraint =
    meta_pointer_confinement_wayland_get_wayland_pointer_constraint (confinement);
  MetaWaylandSurface *surface =
    meta_wayland_pointer_constraint_get_surface (wayland_constraint);
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterInputDevice *pointer = clutter_seat_get_pointer (seat);
  MetaPointerConstraint *constraint;
  graphene_point_t point;
  MtkRectangle rect;
  g_autoptr (MtkRegion) region = NULL;
  float sx, sy, x, y;

  clutter_seat_query_state (seat, pointer, NULL, &point, NULL);
  wayland_constraint =
    meta_pointer_confinement_wayland_get_wayland_pointer_constraint (confinement);
  surface = meta_wayland_pointer_constraint_get_surface (wayland_constraint);
  meta_wayland_surface_get_relative_coordinates (surface,
                                                 point.x, point.y,
                                                 &sx, &sy);

  meta_wayland_surface_get_absolute_coordinates (surface, sx, sy, &x, &y);
  rect = (MtkRectangle) { .x = x, .y = y, .width = 1, .height = 1 };
  region = mtk_region_create_rectangle (&rect);

  constraint = meta_pointer_constraint_new (g_steal_pointer (&region), 0.0);

  return constraint;
}

MetaPointerConfinementWayland *
meta_pointer_lock_wayland_new (MetaWaylandPointerConstraint *constraint)
{
  return g_object_new (META_TYPE_POINTER_LOCK_WAYLAND,
                       "wayland-pointer-constraint", constraint,
                       NULL);
}

static void
meta_pointer_lock_wayland_init (MetaPointerLockWayland *lock_wayland)
{
}

static void
meta_pointer_lock_wayland_class_init (MetaPointerLockWaylandClass *klass)
{
  MetaPointerConfinementWaylandClass *confinement_class =
    META_POINTER_CONFINEMENT_WAYLAND_CLASS (klass);

  confinement_class->create_constraint =
    meta_pointer_lock_wayland_create_constraint;
}
