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

#include "clutter/clutter-snapshot-private.h"

#include "clutter/clutter-paint-context-private.h"
#include "clutter/clutter-paint-node-private.h"
#include "clutter/clutter-paint-nodes.h"

typedef struct _ClutterSnapshotState ClutterSnapshotState;

typedef ClutterPaintNode * (* ClutterSnapshotCollectFunc) (ClutterSnapshot            *snapshot,
                                                           const ClutterSnapshotState *state);

struct _ClutterSnapshotState
{
  ClutterSnapshotCollectFunc collect_func;
  ClutterPaintNode *node;
  uint32_t n_saves;
};

struct _ClutterSnapshot
{
  GObject parent_instance;

  ClutterPaintContext *paint_context;
  GArray *states;
};

G_DEFINE_FINAL_TYPE (ClutterSnapshot, clutter_snapshot, G_TYPE_OBJECT)

static ClutterPaintNode *
collect_default (ClutterSnapshot            *snapshot,
                 const ClutterSnapshotState *state)
{
  gboolean has_operations = state->node->operations != NULL &&
                            state->node->operations->len > 0;
  gboolean has_children = state->node->n_children > 0;

  if (!has_children && !has_operations)
    return NULL;

  return clutter_paint_node_ref (state->node);
}

static inline ClutterSnapshotState *
get_current_state (ClutterSnapshot *snapshot)
{
  unsigned int size = snapshot->states->len;

  g_assert (size > 0);

  return &g_array_index (snapshot->states, ClutterSnapshotState, size - 1);
}

static inline void
push_state (ClutterSnapshot            *snapshot,
            ClutterSnapshotCollectFunc  collect_func,
            ClutterPaintNode           *node)
{
  ClutterSnapshotState state = {
    .collect_func = collect_func,
    .node = node,
  };

  g_array_append_val (snapshot->states, state);
}

static void
snapshot_state_clear (ClutterSnapshotState *state)
{
  g_clear_pointer (&state->node, clutter_paint_node_unref);
}

static void
clutter_snapshot_finalize (GObject *object)
{
  ClutterSnapshot *self = (ClutterSnapshot *)object;

  g_clear_pointer (&self->paint_context, clutter_paint_context_unref);
  g_clear_pointer (&self->states, g_array_unref);

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
  snapshot->states = g_array_new (FALSE, FALSE, sizeof (ClutterSnapshotState));
  g_array_set_clear_func (snapshot->states,
                          (GDestroyNotify) snapshot_state_clear);
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
  ClutterPaintNode *root_node;
  ClutterSnapshot *snapshot;
  CoglFramebuffer *framebuffer;

  g_return_val_if_fail (paint_context != NULL, NULL);

  snapshot = g_object_new (CLUTTER_TYPE_SNAPSHOT, NULL);
  snapshot->paint_context = clutter_paint_context_ref (paint_context);

  /* Ensure at least one node */
  framebuffer = clutter_paint_context_get_base_framebuffer (paint_context);
  root_node = clutter_root_node_new (framebuffer,
                                     &COGL_COLOR_INIT (0x00, 0x00, 0x00, 0x00),
                                     0);
  push_state (snapshot, collect_default, root_node);

  return snapshot;
}

/**
 * clutter_snapshot_free_to_node:
 * @snapshot: a #ClutterSnapshot
 *
 * Frees @snapshot and returns the root node of its render tree.
 *
 * Returns: (transfer full): a #ClutterPaintNode
 */
ClutterPaintNode *
clutter_snapshot_free_to_node (ClutterSnapshot *snapshot)
{
  const ClutterSnapshotState *state;
  ClutterPaintNode *root_node;
  unsigned int forgotten_pops = 0;
  unsigned int i;

  g_return_val_if_fail (CLUTTER_IS_SNAPSHOT (snapshot), NULL);

  g_assert (snapshot->states->len > 0);
  forgotten_pops = snapshot->states->len - 1;

  for (i = forgotten_pops; i >= 1; i--)
    clutter_snapshot_pop (snapshot);

  if (forgotten_pops)
    g_warning ("Too many clutter_snapshot_push_*() calls. %u pops remaining.", forgotten_pops);

  g_assert (snapshot->states->len == 1);

  state = get_current_state (snapshot);
  root_node = clutter_paint_node_ref (state->node);
  g_object_unref (snapshot);

  return root_node;
}

/**
 * clutter_snapshot_pop:
 * @snapshot: a #ClutterSnapshot
 *
 * Removes the top element from the stack of render nodes, and
 * appends it to the node underneath it.
 */
void
clutter_snapshot_pop (ClutterSnapshot *snapshot)
{
  unsigned int size;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  size = snapshot->states->len;

  if (size > 1)
    {
      const ClutterSnapshotState *current_state;
      const ClutterSnapshotState *parent_state;
      ClutterPaintNode *node;

      current_state = get_current_state (snapshot);

      if (current_state->n_saves > 0)
        {
          g_warning ("Trying to pop a state saved with clutter_snapshot_save(), "
                     "but clutter_snapshot_restore() should have been called "
                     "first.");
          return;
        }

      parent_state =
        &g_array_index (snapshot->states, ClutterSnapshotState, size - 2);

      node = current_state->collect_func (snapshot, current_state);

      if (node)
        clutter_paint_node_add_child (parent_state->node, current_state->node);

      g_array_remove_index (snapshot->states, size - 1);

      g_clear_pointer (&node, clutter_paint_node_unref);
    }
  else
    {
      g_warning ("Unexpected pop without a corresponding push");
    }
}

/**
 * clutter_snapshot_add_rectangle:
 * @snapshot: a #ClutterSnapshot
 * @rect: a #ClutterActorBox
 *
 * Adds a rectangle region to the @snapshot, as described by the
 * passed @rect. This rectangle will paint the current operation.
 */
void
clutter_snapshot_add_rectangle (ClutterSnapshot       *snapshot,
                                const ClutterActorBox *rect)
{
  const ClutterSnapshotState *current_state;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  if (snapshot->states->len == 1)
    {
      g_warning ("No paint operation was pushed");
      return;
    }

  current_state = get_current_state (snapshot);
  clutter_paint_node_add_rectangle (current_state->node, rect);
}

/**
 * clutter_snapshot_add_texture_rectangle:
 * @snapshot: a #ClutterSnapshot
 * @rect: a #ClutterActorBox
 * @x_1: the left X coordinate of the texture
 * @y_1: the top Y coordinate of the texture
 * @x_2: the right X coordinate of the texture
 * @y_2: the bottom Y coordinate of the texture
 *
 * Adds a rectangle region to the @snapshot, with texture coordinates.
 */
void
clutter_snapshot_add_texture_rectangle (ClutterSnapshot       *snapshot,
                                        const ClutterActorBox *rect,
                                        float                  x_1,
                                        float                  y_1,
                                        float                  x_2,
                                        float                  y_2)
{
  const ClutterSnapshotState *current_state;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  if (snapshot->states->len == 1)
    {
      g_warning ("No paint operation was pushed");
      return;
    }

  current_state = get_current_state (snapshot);
  clutter_paint_node_add_texture_rectangle (current_state->node,
                                            rect,
                                            x_1, y_1,
                                            x_2, y_2);
}

/**
 * clutter_snapshot_add_multitexture_rectangle:
 * @snapshot: a #ClutterSnapshot
 * @rect: a #ClutterActorBox
 * @text_coords: array of multitexture values
 * @text_coords_len: number of items of @text_coords
 *
 * Adds a rectangle region to the @snapshot, with multitexture coordinates.
 */
void
clutter_snapshot_add_multitexture_rectangle (ClutterSnapshot       *snapshot,
                                             const ClutterActorBox *rect,
                                             const float           *text_coords,
                                             unsigned int           text_coords_len)
{
  const ClutterSnapshotState *current_state;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  if (snapshot->states->len == 1)
    {
      g_warning ("No paint operation was pushed");
      return;
    }

  current_state = get_current_state (snapshot);
  clutter_paint_node_add_multitexture_rectangle (current_state->node,
                                                 rect,
                                                 text_coords,
                                                 text_coords_len);
}

/**
 * clutter_snapshot_add_rectangles:
 * @snapshot: a #ClutterSnapshot
 * @coords: (in) (array length=n_rects) (transfer none): array of
 *   coordinates containing groups of 4 float values: [x_1, y_1, x_2, y_2] that
 *   are interpreted as two position coordinates; one for the top left of the
 *   rectangle (x1, y1), and one for the bottom right of the rectangle
 *   (x2, y2).
 * @n_rects: number of rectangles defined in @coords.
 *
 * Adds a series of rectangles to @snapshot.
 *
 * As a general rule for better performance its recommended to use this API
 * instead of calling clutter_snapshot_add_rectangle() separately for
 * multiple rectangles if all of the rectangles will be drawn together.
 *
 * See cogl_framebuffer_draw_rectangles().
 */
void
clutter_snapshot_add_rectangles (ClutterSnapshot *snapshot,
                                 const float     *coords,
                                 unsigned int     n_rects)
{
  const ClutterSnapshotState *current_state;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  if (snapshot->states->len == 1)
    {
      g_warning ("No paint operation was pushed");
      return;
    }

  current_state = get_current_state (snapshot);
  clutter_paint_node_add_rectangles (current_state->node, coords, n_rects);
}

/**
 * clutter_snapshot_add_texture_rectangles:
 * @snapshot: a #ClutterSnapshot
 * @coords: (in) (array length=n_rects) (transfer none): array containing
 *   groups of 8 float values: [x_1, y_1, x_2, y_2, s_1, t_1, s_2, t_2]
 *   that have the same meaning as the arguments for
 *   cogl_framebuffer_draw_textured_rectangle().
 * @n_rects: number of rectangles defined in @coords.
 *
 * Adds a series of rectangles to @snapshot.
 *
 * The given texture coordinates should always be normalized such that
 * (0, 0) corresponds to the top left and (1, 1) corresponds to the
 * bottom right. To map an entire texture across the rectangle pass
 * in s_1=0, t_1=0, s_2=1, t_2=1.
 *
 * See cogl_framebuffer_draw_textured_rectangles().
 */
void
clutter_snapshot_add_texture_rectangles (ClutterSnapshot *snapshot,
                                         const float     *coords,
                                         unsigned int     n_rects)
{
  const ClutterSnapshotState *current_state;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  if (snapshot->states->len == 1)
    {
      g_warning ("No paint operation was pushed");
      return;
    }

  current_state = get_current_state (snapshot);
  clutter_paint_node_add_texture_rectangles (current_state->node,
                                             coords,
                                             n_rects);
}

/**
 * clutter_snapshot_add_primitive: (skip)
 * @snapshot: a #ClutterSnapshot
 * @primitive: a Cogl primitive
 *
 * Adds a region described by a #CoglPrimitive to the @snapshot.
 *
 * This function acquires a reference on @primitive, so it is safe
 * to call cogl_object_unref() when it returns.
 */
void
clutter_snapshot_add_primitive (ClutterSnapshot *snapshot,
                                CoglPrimitive   *primitive)
{

  const ClutterSnapshotState *current_state;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  if (snapshot->states->len == 1)
    {
      g_warning ("No paint operation was pushed");
      return;
    }

  current_state = get_current_state (snapshot);
  clutter_paint_node_add_primitive (current_state->node, primitive);
}

/**
 * clutter_snapshot_add_node:
 * @snapshot: a #ClutterSnapshot
 * @node: a #ClutterPaintNode
 *
 * Adds the @snapshot to the previously saved state.
 *
 * It is a programming error to call this function without a prior call
 * to clutter_snapshot_save().
 */
void
clutter_snapshot_add_node (ClutterSnapshot  *snapshot,
                           ClutterPaintNode *node)
{
  const ClutterSnapshotState *current_state;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));
  g_return_if_fail (node != NULL);

  if (snapshot->states->len == 0)
    {
      g_warning ("Cannot add node to an empty snapshot");
      return;
    }

  current_state = get_current_state (snapshot);
  clutter_paint_node_add_child (current_state->node, node);
}

/**
 * clutter_snapshot_push_clip:
 * @snapshot: a #ClutterSnapshot
 *
 * Pushes a clip node to @snapshot.
 */
void
clutter_snapshot_push_clip (ClutterSnapshot *snapshot)
{
  ClutterPaintNode *clip_node;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  clip_node = clutter_clip_node_new ();
  push_state (snapshot, collect_default, clip_node);
}

/**
 * clutter_snapshot_push_color:
 * @snapshot: a #ClutterSnapshot
 *
 * Pushes a color node to @snapshot.
 */
void
clutter_snapshot_push_color (ClutterSnapshot *snapshot,
                             const CoglColor *color)
{
  ClutterPaintNode *color_node;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  color_node = clutter_color_node_new (color);
  push_state (snapshot, collect_default, color_node);
}

/**
 * clutter_snapshot_push_rotate:
 * @snapshot: a #ClutterSnapshot
 * @angle: the rotation angle
 *
 * Rotates @snapshot's coordinate system by @angle degrees in 2D space -
 * or in 3D speak, rotates around the Z axis.
 */
void
clutter_snapshot_push_rotate (ClutterSnapshot *snapshot,
                              float            angle)
{
  graphene_matrix_t rotation;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  graphene_matrix_init_rotate (&rotation, angle, graphene_vec3_z_axis ());
  clutter_snapshot_push_transform (snapshot, &rotation);
}

/**
 * clutter_snapshot_push_rotate_3d:
 * @snapshot: a #ClutterSnapshot
 * @angle: the rotation angle
 * @axis: the rotation angle
 *
 * Rotates @snapshot's coordinate system by @angle degrees around @axis
 */
void
clutter_snapshot_push_rotate_3d (ClutterSnapshot       *snapshot,
                                 float                  angle,
                                 const graphene_vec3_t *axis)
{
  graphene_matrix_t rotation;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));
  g_return_if_fail (axis != NULL);

  graphene_matrix_init_rotate (&rotation, angle, axis);
  clutter_snapshot_push_transform (snapshot, &rotation);
}

/**
 * clutter_snapshot_push_scale:
 * @snapshot: a #ClutterSnapshot
 * @factor_x: scaling factor on the X axis
 * @factor_y: scaling factor on the Y axis
 *
 * Scales @snapshot's coordinate system in 2-dimensional space by
 * the given factors.
 */
void
clutter_snapshot_push_scale (ClutterSnapshot *snapshot,
                             float            factor_x,
                             float            factor_y)
{
  graphene_matrix_t scale;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  graphene_matrix_init_scale (&scale, factor_x, factor_y, 1.0);
  clutter_snapshot_push_transform (snapshot, &scale);
}

/**
 * clutter_snapshot_push_scale_3d:
 * @snapshot: a #ClutterSnapshot
 * @factor_x: scaling factor on the X axis
 * @factor_y: scaling factor on the Y axis
 * @factor_z: scaling factor on the Z axis
 *
 * Scales @snapshot's coordinate system by the given factors.
 */
void
clutter_snapshot_push_scale_3d (ClutterSnapshot *snapshot,
                                float            factor_x,
                                float            factor_y,
                                float            factor_z)
{
  graphene_matrix_t scale;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  graphene_matrix_init_scale (&scale, factor_x, factor_y, factor_z);
  clutter_snapshot_push_transform (snapshot, &scale);
}

/**
 * clutter_snapshot_push_texture:
 * @snapshot: a #ClutterSnapshot
 * @texture: a #CoglTexture
 *
 * Pushes a texture node to @snapshot.
 */
void
clutter_snapshot_push_texture (ClutterSnapshot *snapshot,
                               CoglTexture     *texture)
{
  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));
  g_return_if_fail (COGL_IS_TEXTURE (texture));

  clutter_snapshot_push_texture_full (snapshot,
                                      texture,
                                      NULL,
                                      CLUTTER_SCALING_FILTER_NEAREST,
                                      CLUTTER_SCALING_FILTER_NEAREST);
}

/**
 * clutter_snapshot_push_texture_full:
 * @snapshot: a #ClutterSnapshot
 * @texture: a #CoglTexture
 * @blend_color: (nullable): a #CoglColor used for blending
 * @min_filter: the minification filter for the texture
 * @mag_filter: the magnification filter for the texture
 *
 * Pushes a texture node to @snapshot. If @color is %NULL, it is assumed
 * to be fully opaque white.
 */
void
clutter_snapshot_push_texture_full (ClutterSnapshot      *snapshot,
                                    CoglTexture          *texture,
                                    const CoglColor      *blend_color,
                                    ClutterScalingFilter  min_filter,
                                    ClutterScalingFilter  mag_filter)
{
  ClutterPaintNode *texture_node;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  texture_node = clutter_texture_node_new (texture,
                                           blend_color,
                                           min_filter,
                                           mag_filter);

  push_state (snapshot, collect_default, texture_node);
}

/**
 * clutter_snapshot_push_transform:
 * @snapshot: a #ClutterSnapshot
 * @transform: a #graphene_matrix_t
 *
 * Pushes a transform node to @snapshot.
 */
void
clutter_snapshot_push_transform (ClutterSnapshot         *snapshot,
                                 const graphene_matrix_t *transform)
{
  ClutterPaintNode *transform_node;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));
  g_return_if_fail (transform != NULL);

  transform_node = clutter_transform_node_new (transform);
  push_state (snapshot, collect_default, transform_node);
}

/**
 * clutter_snapshot_push_translate:
 * @snapshot: a #ClutterSnapshot
 * @point: a #graphene_point_t
 *
 * Pushes a 2D translation node to @snapshot.
 */
void
clutter_snapshot_push_translate (ClutterSnapshot        *snapshot,
                                 const graphene_point_t *point)
{
  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));
  g_return_if_fail (point != NULL);

  clutter_snapshot_push_translate_3d (snapshot,
                                      &GRAPHENE_POINT3D_INIT (point->x,
                                                              point->y,
                                                              0.0));
}

/**
 * clutter_snapshot_push_translate_3d:
 * @snapshot: a #ClutterSnapshot
 * @point: a #graphene_point3d_t
 *
 * Pushes a 3D translation node to @snapshot.
 */
void
clutter_snapshot_push_translate_3d (ClutterSnapshot          *snapshot,
                                    const graphene_point3d_t *point)
{
  graphene_matrix_t translate;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));
  g_return_if_fail (point != NULL);

  graphene_matrix_init_translate (&translate, point);
  clutter_snapshot_push_transform (snapshot, &translate);
}

/**
 * clutter_snapshot_save:
 * @snapshot: a #ClutterSnapshot
 *
 * Saves the current state of @snapshot. When the corresponding call to
 * clutter_snapshot_restore() is executed, @snapshot will return to the
 * current state.
 *
 * All calls to clutter_snapshot_save() must have a corresponding call to
 * clutter_snapshot_restore().
 *
 * Multiple calls to clutter_snapshot_save() can be nested.
 */
void
clutter_snapshot_save (ClutterSnapshot *snapshot)
{
  ClutterSnapshotState *current_state;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  current_state = get_current_state (snapshot);
  current_state->n_saves++;
}

/**
 * clutter_snapshot_restore:
 * @snapshot: a #ClutterSnapshot
 *
 * Restores @snapshot to the previously saved state.
 *
 * It is a programming error to call this function without a prior call
 * to clutter_snapshot_save().
 */
void
clutter_snapshot_restore (ClutterSnapshot *snapshot)
{
  ClutterSnapshotState *saved_state;
  unsigned int n_pops = 0;
  gboolean found_save;
  int i;

  g_return_if_fail (CLUTTER_IS_SNAPSHOT (snapshot));

  found_save = FALSE;
  for (i = snapshot->states->len - 1; i >= 0; i--)
    {
      saved_state = &g_array_index (snapshot->states, ClutterSnapshotState, i);
      found_save |= saved_state->n_saves > 0;
      if (found_save)
        break;
      n_pops++;
    }

  if (!found_save)
    {
      g_warning ("Unpaired call to clutter_snapshot_restore() without a matching clutter_snapshot_save()");
      return;
    }

  saved_state->n_saves--;

  while (n_pops-- > 0)
    clutter_snapshot_pop (snapshot);
}

/*<private>
 * clutter_snapshot_get_paint_context: (skip)
 * @snapshot: a #ClutterSnapshot
 *
 * Retrieves the #ClutterPaintContext that @snapshot was created with.
 *
 * Returns: (transfer none): a #ClutterPaintContext
 */
ClutterPaintContext *
clutter_snapshot_get_paint_context (ClutterSnapshot *snapshot)
{
  g_assert (CLUTTER_IS_SNAPSHOT (snapshot));

  return snapshot->paint_context;
}
