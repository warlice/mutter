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

#ifndef CLUTTER_SNAPSHOT_H
#define CLUTTER_SNAPSHOT_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-types.h>
#include <clutter/clutter-paint-context.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_SNAPSHOT (clutter_snapshot_get_type())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterSnapshot, clutter_snapshot, CLUTTER, SNAPSHOT, GObject)

CLUTTER_EXPORT
ClutterSnapshot * clutter_snapshot_new (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
ClutterPaintNode * clutter_snapshot_free_to_node (ClutterSnapshot *snapshot);

CLUTTER_EXPORT
void clutter_snapshot_pop (ClutterSnapshot *snapshot);

CLUTTER_EXPORT
void clutter_snapshot_push_clip (ClutterSnapshot *snapshot);

CLUTTER_EXPORT
void clutter_snapshot_push_color (ClutterSnapshot *snapshot,
                                  const CoglColor *color);

CLUTTER_EXPORT
void clutter_snapshot_push_rotate (ClutterSnapshot *snapshot,
                                   float            angle);

CLUTTER_EXPORT
void clutter_snapshot_push_rotate_3d (ClutterSnapshot       *snapshot,
                                      float                  angle,
                                      const graphene_vec3_t *axis);

CLUTTER_EXPORT
void clutter_snapshot_push_scale (ClutterSnapshot *snapshot,
                                  float            factor_x,
                                  float            factor_y);

CLUTTER_EXPORT
void clutter_snapshot_push_scale_3d (ClutterSnapshot *snapshot,
                                     float            factor_x,
                                     float            factor_y,
                                     float            factor_z);

CLUTTER_EXPORT
void clutter_snapshot_push_texture (ClutterSnapshot *snapshot,
                                    CoglTexture     *texture);

CLUTTER_EXPORT
void clutter_snapshot_push_texture_full (ClutterSnapshot      *snapshot,
                                         CoglTexture          *texture,
                                         const CoglColor      *blend_color,
                                         ClutterScalingFilter  min_filter,
                                         ClutterScalingFilter  mag_filter);

CLUTTER_EXPORT
void clutter_snapshot_push_transform (ClutterSnapshot         *snapshot,
                                      const graphene_matrix_t *transform);

CLUTTER_EXPORT
void clutter_snapshot_push_translate (ClutterSnapshot        *snapshot,
                                      const graphene_point_t *point);

CLUTTER_EXPORT
void clutter_snapshot_push_translate_3d (ClutterSnapshot          *snapshot,
                                         const graphene_point3d_t *point);

CLUTTER_EXPORT
void clutter_snapshot_add_rectangle (ClutterSnapshot       *snapshot,
                                     const ClutterActorBox *rect);

CLUTTER_EXPORT
void clutter_snapshot_add_texture_rectangle (ClutterSnapshot       *snapshot,
                                             const ClutterActorBox *rect,
                                             float                  x_1,
                                             float                  y_1,
                                             float                  x_2,
                                             float                  y_2);

CLUTTER_EXPORT
void clutter_snapshot_add_multitexture_rectangle (ClutterSnapshot       *snapshot,
                                                  const ClutterActorBox *rect,
                                                  const float           *text_coords,
                                                  unsigned int           text_coords_len);

CLUTTER_EXPORT
void clutter_snapshot_add_rectangles (ClutterSnapshot *snapshot,
                                      const float     *coords,
                                      unsigned int     n_rects);
CLUTTER_EXPORT
void clutter_snapshot_add_texture_rectangles (ClutterSnapshot *snapshot,
                                              const float     *coords,
                                              unsigned int     n_rects);

CLUTTER_EXPORT
void clutter_snapshot_add_primitive (ClutterSnapshot *snapshot,
                                     CoglPrimitive   *primitive);

G_END_DECLS

#endif /* CLUTTER_SNAPSHOT_H */
