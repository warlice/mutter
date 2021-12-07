/*
 * Copyright (C) 2021 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "config.h"

#include "clutter-snapshot.h"

#include "clutter/clutter-paint-context.h"

struct _ClutterSnapshot
{
  GObject parent_instance;

  ClutterPaintContext *paint_context;
};

G_DEFINE_FINAL_TYPE (ClutterSnapshot, clutter_snapshot, G_TYPE_OBJECT)

static void
clutter_snapshot_finalize (GObject *object)
{
  ClutterSnapshot *self = (ClutterSnapshot *)object;

  g_clear_pointer (&self->paint_context, clutter_paint_context_unref);

  G_OBJECT_CLASS (clutter_snapshot_parent_class)->finalize (object);
}

static void
clutter_snapshot_class_init (ClutterSnapshotClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = clutter_snapshot_finalize;
}

static void
clutter_snapshot_init (ClutterSnapshot *snapshot)
{
}

/**
 * clutter_snapshot_new:
 * @paint_context: a #ClutterPaintContext
 *
 * Creates a new #ClutterSnapshot operating on @paint_context.
 *
 * Returns: (transfer full): a #ClutterSnapshot
 */
ClutterSnapshot *
clutter_snapshot_new (ClutterPaintContext *paint_context)
{
  ClutterSnapshot *snapshot;

  g_return_val_if_fail (paint_context != NULL, NULL);

  snapshot = g_object_new (CLUTTER_TYPE_SNAPSHOT, NULL);
  snapshot->paint_context = clutter_paint_context_ref (paint_context);

  return snapshot;
}
