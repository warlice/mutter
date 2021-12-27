#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static ClutterPaintContext *
create_offscreen_paint_context (int width,
                                int height)
{
  g_autoptr (ClutterPaintContext) paint_context = NULL;
  g_autoptr (CoglOffscreen) offscreen = NULL;
  g_autoptr (CoglTexture) bitmap_texture = NULL;
  g_autoptr (GError) error = NULL;
  ClutterBackend *clutter_backend;
  CoglContext *cogl_context;

  clutter_backend = clutter_get_default_backend ();
  g_assert_nonnull (clutter_backend);

  cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  g_assert_nonnull (cogl_context);

  bitmap_texture = cogl_texture_2d_new_with_size (cogl_context, width, height);
  cogl_primitive_texture_set_auto_mipmap (bitmap_texture, FALSE);
  cogl_texture_allocate (COGL_TEXTURE (bitmap_texture), &error);
  g_assert_no_error (error);

  offscreen = cogl_offscreen_new_with_texture (COGL_TEXTURE (bitmap_texture));
  cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error);
  g_assert_no_error (error);

  paint_context =
    clutter_paint_context_new_for_framebuffer (COGL_FRAMEBUFFER (offscreen),
                                               NULL,
                                               CLUTTER_PAINT_FLAG_CLEAR);
  g_assert_nonnull (paint_context);

  return g_steal_pointer (&paint_context);
}

static void
snapshot_new (void)
{
  g_autoptr (ClutterPaintContext) paint_context = NULL;
  g_autoptr (ClutterSnapshot) snapshot = NULL;

  paint_context = create_offscreen_paint_context (800, 600);
  snapshot = clutter_snapshot_new (paint_context);
  g_assert_nonnull (snapshot);
}

static void
snapshot_save_restore (void)
{
  g_autoptr (ClutterPaintContext) paint_context = NULL;
  g_autoptr (ClutterSnapshot) snapshot = NULL;

  paint_context = create_offscreen_paint_context (800, 600);
  snapshot = clutter_snapshot_new (paint_context);
  g_assert_nonnull (snapshot);

  clutter_snapshot_save (snapshot);
  clutter_snapshot_push_translate (snapshot, &GRAPHENE_POINT_INIT (20.0, 20.0));
  clutter_snapshot_push_scale (snapshot, 10.0, -5.0);
  clutter_snapshot_push_color (snapshot, &COGL_COLOR_INIT (0xff, 0x00, 0x00, 0x00));
  clutter_snapshot_restore (snapshot);


  clutter_snapshot_save (snapshot); // 1
  clutter_snapshot_push_translate (snapshot, &GRAPHENE_POINT_INIT (20.0, 20.0));
  clutter_snapshot_push_scale (snapshot, 10.0, -5.0);
  clutter_snapshot_save (snapshot); // 2
  clutter_snapshot_save (snapshot); // 3
  clutter_snapshot_push_color (snapshot, &COGL_COLOR_INIT (0xff, 0x00, 0x00, 0x00));
  clutter_snapshot_restore (snapshot); // 3
  clutter_snapshot_restore (snapshot); // 2
  clutter_snapshot_push_scale (snapshot, 10.0, -5.0);
  clutter_snapshot_push_translate (snapshot, &GRAPHENE_POINT_INIT (20.0, 20.0));
  clutter_snapshot_restore (snapshot); // 1
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/snapshot/new", snapshot_new)
  CLUTTER_TEST_UNIT ("/snapshot/save-restore", snapshot_save_restore)
)
