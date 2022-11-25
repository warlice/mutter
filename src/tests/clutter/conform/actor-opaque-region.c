#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static void
on_after_paint (ClutterStage     *stage,
                ClutterStageView *view,
                gboolean         *was_painted)
{
  *was_painted = TRUE;
}

static void
wait_for_paint (ClutterActor *stage)
{
  gboolean was_painted = FALSE;
  gulong was_painted_id;

  was_painted_id = g_signal_connect (CLUTTER_STAGE (stage),
                                     "after-paint",
                                     G_CALLBACK (on_after_paint),
                                     &was_painted);

  while (!was_painted)
    g_main_context_iteration (NULL, FALSE);

  g_signal_handler_disconnect (stage, was_painted_id);
}

static cairo_region_t *
get_stage_region (ClutterActor *stage)
{
  ClutterActorBox allocation;
  cairo_rectangle_int_t rect;

  clutter_actor_get_allocation_box (stage, &allocation);
  rect.x = clutter_actor_box_get_x (&allocation);
  rect.y = clutter_actor_box_get_y (&allocation);
  rect.width = clutter_actor_box_get_width (&allocation);
  rect.height = clutter_actor_box_get_height (&allocation);

  return cairo_region_create_rectangle (&rect);
}

static void
rectangle_int_contained (const graphene_rect_t *src,
                         cairo_rectangle_int_t *dest)
{
  /* Copied from clutter_util_rectangle_int_contained() */
  dest->x = G_APPROX_VALUE (roundf (src->origin.x), src->origin.x, 0.001)
    ? roundf (src->origin.x) : ceilf (src->origin.x);
  dest->y = G_APPROX_VALUE (roundf (src->origin.y), src->origin.y, 0.001)
    ? roundf (src->origin.y) : ceilf (src->origin.y);
  dest->width = G_APPROX_VALUE (roundf (src->size.width), src->size.width, 0.001)
    ? roundf (src->size.width) : floorf (src->size.width);
  dest->height = G_APPROX_VALUE (roundf (src->size.height), src->size.height, 0.001)
    ? roundf (src->size.height) : floorf (src->size.height);
}

static void
transform_points_to_stage_rect (ClutterActor          *actor,
                                graphene_rect_t        in_rect,
                                cairo_rectangle_int_t *out_rect)
{
  graphene_point3d_t in_vertices_1, out_vertices_1;
  graphene_point3d_t in_vertices_2, out_vertices_2;

  graphene_rect_t transformed_rect;

  in_vertices_1.x = in_rect.origin.x;
  in_vertices_1.y = in_rect.origin.y;
  in_vertices_1.z = 0;
  in_vertices_2.x = in_vertices_1.x + in_rect.size.width;
  in_vertices_2.y = in_vertices_1.y + in_rect.size.height;
  in_vertices_2.z = 0;

  clutter_actor_apply_transform_to_point (actor, &in_vertices_1, &out_vertices_1);
  clutter_actor_apply_transform_to_point (actor, &in_vertices_2, &out_vertices_2);

  transformed_rect = (graphene_rect_t) {
    .origin.x = out_vertices_1.x,
    .origin.y = out_vertices_1.y,
    .size.width = out_vertices_2.x - out_vertices_1.x,
    .size.height =  out_vertices_2.y - out_vertices_1.y,
  };

  rectangle_int_contained (&transformed_rect, out_rect);
}

static void
subtract_actor_absolute_rectangle (cairo_region_t *region,
                                   ClutterActor   *actor)
{
  graphene_rect_t transformed_extents;
  cairo_rectangle_int_t rect;

  clutter_actor_get_transformed_extents (actor, &transformed_extents);
  rectangle_int_contained (&transformed_extents, &rect);

  cairo_region_subtract_rectangle (region, &rect);
}

static void
actor_opaque_region_opacity (void)
{
  ClutterActor *stage;
  ClutterActor *actor_1;
  ClutterActor *actor_2;
  ClutterActor *actor_3;
  cairo_region_t *reference_unobscured_region;
  cairo_region_t *unobscured_region_actor_3;
  cairo_region_t *unobscured_region_actor_2;
  cairo_region_t *unobscured_region_actor_1;
  cairo_region_t *unobscured_region_stage;

  stage = clutter_test_get_stage ();

  clutter_actor_set_size (stage, 1000, 1000);

  actor_1 = clutter_actor_new ();
  clutter_actor_set_background_color (actor_1, CLUTTER_COLOR_Green);
  clutter_actor_set_size (actor_1, 100, 100);

  actor_2 = clutter_actor_new ();
  clutter_actor_set_background_color (actor_2, CLUTTER_COLOR_Orange);
  clutter_actor_set_x (actor_2, 90);
  clutter_actor_set_size (actor_2, 20, 20);
  clutter_actor_set_opacity (actor_2, 200);

  actor_3 = clutter_actor_new ();
  clutter_actor_set_background_color (actor_3, CLUTTER_COLOR_Blue);
  clutter_actor_set_x (actor_3, 80);
  clutter_actor_set_y (actor_3, 80);
  clutter_actor_set_size (actor_3, 40, 35);

  clutter_actor_add_child (stage, actor_1);
  clutter_actor_add_child (stage, actor_2);
  clutter_actor_add_child (stage, actor_3);

  clutter_actor_show (stage);

  wait_for_paint (stage);

  unobscured_region_actor_3 = clutter_actor_peek_unobscured_region (actor_3);
  unobscured_region_actor_2 = clutter_actor_peek_unobscured_region (actor_2);
  unobscured_region_actor_1 = clutter_actor_peek_unobscured_region (actor_1);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_3));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_3);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_2));
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_1));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_1);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  clutter_actor_set_opacity (actor_2, 255);

  wait_for_paint (stage);

  unobscured_region_actor_3 = clutter_actor_peek_unobscured_region (actor_3);
  unobscured_region_actor_2 = clutter_actor_peek_unobscured_region (actor_2);
  unobscured_region_actor_1 = clutter_actor_peek_unobscured_region (actor_1);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_3));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_3);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_2));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_2);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_1));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_1);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  clutter_actor_destroy (actor_3);
  clutter_actor_destroy (actor_2);
  clutter_actor_destroy (actor_1);
}

static void
actor_opaque_region_hide_child (void)
{
  ClutterActor *stage;
  ClutterActor *actor_1;
  ClutterActor *actor_2;
  cairo_region_t *reference_unobscured_region;
  cairo_region_t *unobscured_region_actor_2;
  cairo_region_t *unobscured_region_actor_1;
  cairo_region_t *unobscured_region_stage;

  stage = clutter_test_get_stage ();

  clutter_actor_set_size (stage, 1000, 1000);

  actor_1 = clutter_actor_new ();
  clutter_actor_set_background_color (actor_1, CLUTTER_COLOR_Green);
  clutter_actor_set_size (actor_1, 1000, 1000);

  actor_2 = clutter_actor_new ();
  clutter_actor_set_background_color (actor_2, CLUTTER_COLOR_Orange);
  clutter_actor_set_x (actor_2, 90);
  clutter_actor_set_size (actor_2, 20, 20);

  clutter_actor_add_child (stage, actor_1);
  clutter_actor_add_child (stage, actor_2);

  clutter_actor_show (stage);

  wait_for_paint (stage);

  unobscured_region_actor_2 = clutter_actor_peek_unobscured_region (actor_2);
  unobscured_region_actor_1 = clutter_actor_peek_unobscured_region (actor_1);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_2));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_2);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_1));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_1);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));
  g_assert (cairo_region_is_empty (unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  clutter_actor_hide (actor_1);

  wait_for_paint (stage);

  unobscured_region_actor_2 = clutter_actor_peek_unobscured_region (actor_2);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_2));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_2);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_1));
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));
  g_assert (!cairo_region_is_empty (unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  clutter_actor_hide (actor_2);
  clutter_actor_show (actor_1);

  wait_for_paint (stage);

  unobscured_region_actor_1 = clutter_actor_peek_unobscured_region (actor_1);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_1));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_1);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));
  g_assert (cairo_region_is_empty (unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  clutter_actor_destroy (actor_2);
  clutter_actor_destroy (actor_1);
}

static void
actor_opaque_region_container_opacity (void)
{
  ClutterActor *stage;
  ClutterActor *container;
  ClutterActor *container_child;
  ClutterActor *actor_2;
  cairo_region_t *reference_unobscured_region;
  cairo_region_t *unobscured_region_container;
  cairo_region_t *unobscured_region_container_child;
  cairo_region_t *unobscured_region_actor_2;
  cairo_region_t *unobscured_region_stage;
  cairo_region_t *last_unobscured_region_container;
  cairo_region_t *last_unobscured_region_container_child;
  cairo_region_t *last_unobscured_region_actor_2;
  cairo_region_t *last_unobscured_region_stage;

  stage = clutter_test_get_stage ();

  clutter_actor_set_size (stage, 1000, 1000);

  container = clutter_actor_new ();
  clutter_actor_set_background_color (container, CLUTTER_COLOR_Green);
  clutter_actor_set_position (container, 250, 250);
  clutter_actor_set_size (container, 200, 400);

  container_child = clutter_actor_new ();
  clutter_actor_set_background_color (container_child, CLUTTER_COLOR_Blue);
  clutter_actor_set_x (container_child, 180);
  clutter_actor_set_size (container_child, 100, 200);

  actor_2 = clutter_actor_new ();
  clutter_actor_set_background_color (actor_2, CLUTTER_COLOR_Red);
  clutter_actor_set_size (actor_2, 500, 500);

  clutter_actor_add_child (container, container_child);
  clutter_actor_add_child (stage, container);
  clutter_actor_add_child (stage, actor_2);

  clutter_actor_show (stage);

  wait_for_paint (stage);

  unobscured_region_actor_2 = clutter_actor_peek_unobscured_region (actor_2);
  unobscured_region_container_child = clutter_actor_peek_unobscured_region (container_child);
  unobscured_region_container = clutter_actor_peek_unobscured_region (container);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_2));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_2);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container_child));

  subtract_actor_absolute_rectangle (reference_unobscured_region, container_child);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container));

  subtract_actor_absolute_rectangle (reference_unobscured_region, container);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  /* Store the old regions and move the container to 0, 0. This should change
   * the unobscured region of the container and the stage.
   */
  last_unobscured_region_actor_2 = cairo_region_copy (unobscured_region_actor_2);
  last_unobscured_region_container_child = cairo_region_copy (unobscured_region_container_child);
  last_unobscured_region_container = cairo_region_copy (unobscured_region_container);
  last_unobscured_region_stage = cairo_region_copy (unobscured_region_stage);
  clutter_actor_set_position (container, 0, 0);

  wait_for_paint (stage);

  unobscured_region_actor_2 = clutter_actor_peek_unobscured_region (actor_2);
  unobscured_region_container_child = clutter_actor_peek_unobscured_region (container_child);
  unobscured_region_container = clutter_actor_peek_unobscured_region (container);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_2));
  g_assert (cairo_region_equal (unobscured_region_actor_2, last_unobscured_region_actor_2));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_2);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container_child));
  g_assert (cairo_region_equal (unobscured_region_container_child, last_unobscured_region_container_child));

  subtract_actor_absolute_rectangle (reference_unobscured_region, container_child);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container));
  g_assert (!cairo_region_equal (unobscured_region_container, last_unobscured_region_container));

  subtract_actor_absolute_rectangle (reference_unobscured_region, container);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));
  g_assert (!cairo_region_equal (unobscured_region_stage, last_unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);
  cairo_region_destroy (last_unobscured_region_actor_2);
  cairo_region_destroy (last_unobscured_region_container_child);
  cairo_region_destroy (last_unobscured_region_container);
  cairo_region_destroy (last_unobscured_region_stage);

  /* Place the container above actor_2 */
  clutter_actor_set_position (container, 900, -10);
  clutter_actor_set_child_above_sibling (stage, container, actor_2);

  clutter_actor_set_scale (actor_2, 5.0, 5.0);

  wait_for_paint (stage);

  unobscured_region_container_child = clutter_actor_peek_unobscured_region (container_child);
  unobscured_region_container = clutter_actor_peek_unobscured_region (container);
  unobscured_region_actor_2 = clutter_actor_peek_unobscured_region (actor_2);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container_child));

  subtract_actor_absolute_rectangle (reference_unobscured_region, container_child);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container));

  subtract_actor_absolute_rectangle (reference_unobscured_region, container);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_2));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_2);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));
  g_assert (cairo_region_is_empty (unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  /* Now make the container transparent, that should make both the container
   * and the child transparent.
   */
  clutter_actor_set_opacity (container, 100);

  wait_for_paint (stage);

  unobscured_region_container_child = clutter_actor_peek_unobscured_region (container_child);
  unobscured_region_container = clutter_actor_peek_unobscured_region (container);
  unobscured_region_actor_2 = clutter_actor_peek_unobscured_region (actor_2);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container_child));
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container));
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_actor_2));

  subtract_actor_absolute_rectangle (reference_unobscured_region, actor_2);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));
  g_assert (cairo_region_is_empty (unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  clutter_actor_destroy (container_child);
  clutter_actor_destroy (container);
  clutter_actor_destroy (actor_2);
}

static void
actor_opaque_region_clip (void)
{
  ClutterActor *stage;
  ClutterActor *container;
  ClutterActor *container_child;
  cairo_region_t *reference_unobscured_region;
  cairo_region_t *unobscured_region_container;
  cairo_region_t *unobscured_region_container_child;
  cairo_region_t *unobscured_region_stage;
  cairo_rectangle_int_t rect;

  stage = clutter_test_get_stage ();

  clutter_actor_set_size (stage, 1000, 1000);

  container = clutter_actor_new ();
  clutter_actor_set_background_color (container, CLUTTER_COLOR_Green);
  clutter_actor_set_position (container, 100, 100);
  clutter_actor_set_size (container, 200.3613, 200.67143);
  clutter_actor_set_clip_to_allocation (container, TRUE);
  clutter_actor_set_scale (container, 3.135313, 4.1211);

  container_child = clutter_actor_new ();
  clutter_actor_set_background_color (container_child, CLUTTER_COLOR_Blue);
  clutter_actor_set_size (container_child, 400, 400);

  clutter_actor_add_child (container, container_child);
  clutter_actor_add_child (stage, container);

  clutter_actor_show (stage);

  wait_for_paint (stage);

  unobscured_region_container_child = clutter_actor_peek_unobscured_region (container_child);
  unobscured_region_container = clutter_actor_peek_unobscured_region (container);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container_child));

  subtract_actor_absolute_rectangle (reference_unobscured_region, container_child);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container));

  cairo_region_destroy (reference_unobscured_region);
  reference_unobscured_region = get_stage_region (stage);
  subtract_actor_absolute_rectangle (reference_unobscured_region, container);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  clutter_actor_set_clip (container, 50.341, 0, 100.13, 100.976453);

  wait_for_paint (stage);

  unobscured_region_container_child = clutter_actor_peek_unobscured_region (container_child);
  unobscured_region_container = clutter_actor_peek_unobscured_region (container);
  unobscured_region_stage = clutter_actor_peek_unobscured_region (stage);

  reference_unobscured_region = get_stage_region (stage);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container_child));

  subtract_actor_absolute_rectangle (reference_unobscured_region, container_child);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_container));

  cairo_region_destroy (reference_unobscured_region);
  reference_unobscured_region = get_stage_region (stage);

  transform_points_to_stage_rect (container, (graphene_rect_t) {
    .origin.x = 50.341,
    .origin.y = 0,
    .size.width = 100.13,
    .size.height = 100.976453,
  }, &rect);
  cairo_region_subtract_rectangle (reference_unobscured_region, &rect);
  g_assert (cairo_region_equal (reference_unobscured_region, unobscured_region_stage));

  cairo_region_destroy (reference_unobscured_region);

  clutter_actor_destroy (container_child);
  clutter_actor_destroy (container);
}


CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/opaque-region/opacity", actor_opaque_region_opacity)
  CLUTTER_TEST_UNIT ("/actor/opaque-region/hide-child", actor_opaque_region_hide_child)
  CLUTTER_TEST_UNIT ("/actor/opaque-region/container-opacity", actor_opaque_region_container_opacity)
  CLUTTER_TEST_UNIT ("/actor/opaque-region/clip", actor_opaque_region_clip)
)
