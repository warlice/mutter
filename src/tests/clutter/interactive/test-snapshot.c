#include <stdlib.h>
#include <gmodule.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

int test_snapshot_main (int argc, char *argv[]);

static ClutterContent *
create_content (int width,
                int height)
{
  g_autoptr (ClutterPaintContext) paint_context = NULL;
  g_autoptr (ClutterPaintNode) node = NULL;
  g_autoptr (ClutterSnapshot) snapshot = NULL;
  g_autoptr (CoglOffscreen) offscreen = NULL;
  g_autoptr (CoglTexture) texture = NULL;
  g_autoptr (GError) error = NULL;
  ClutterBackend *clutter_backend;
  CoglContext *cogl_context;

  clutter_backend = clutter_get_default_backend ();
  g_assert_nonnull (clutter_backend);

  cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  g_assert_nonnull (cogl_context);

  texture = cogl_texture_2d_new_with_size (cogl_context, width, height);
  cogl_primitive_texture_set_auto_mipmap (texture, FALSE);
  cogl_texture_allocate (COGL_TEXTURE (texture), &error);
  g_assert_no_error (error);

  offscreen = cogl_offscreen_new_with_texture (COGL_TEXTURE (texture));
  cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error);
  g_assert_no_error (error);

  cogl_framebuffer_orthographic (COGL_FRAMEBUFFER (offscreen),
                                 0.0, 0.0,
                                 width, height,
                                 0.0, 1.0);
  cogl_framebuffer_set_viewport (COGL_FRAMEBUFFER (offscreen),
                                 0.0, 0.0,
                                 width, height);

  paint_context =
    clutter_paint_context_new_for_framebuffer (COGL_FRAMEBUFFER (offscreen),
                                               NULL,
                                               CLUTTER_PAINT_FLAG_CLEAR);
  g_assert_nonnull (paint_context);


  snapshot = clutter_snapshot_new (paint_context);
  g_assert_nonnull (snapshot);

  /* Paint some content */
  clutter_snapshot_push_color (snapshot, &COGL_COLOR_INIT (0xff, 0x00, 0x00, 0x00));
  clutter_snapshot_add_rectangle (snapshot,
                                  &(ClutterActorBox) {
                                    .x1 = 0, .x2 = 50,
                                    .y1 = 0, .y2 = 50,
                                  });
  clutter_snapshot_add_rectangle (snapshot,
                                  &(ClutterActorBox) {
                                    .x1 = 750, .x2 = 800,
                                    .y1 = 550, .y2 = 600,
                                  });
  clutter_snapshot_pop (snapshot);

  clutter_snapshot_push_color (snapshot, &COGL_COLOR_INIT (0x00, 0xff, 0x00, 0x00));
  clutter_snapshot_add_rectangle (snapshot,
                                  &(ClutterActorBox) {
                                    .x1 = 750, .x2 = 800,
                                    .y1 = 0, .y2 = 50,
                                  });
  clutter_snapshot_add_rectangle (snapshot,
                                  &(ClutterActorBox) {
                                    .x1 = 0, .x2 = 50,
                                    .y1 = 550, .y2 = 600,
                                  });
  clutter_snapshot_pop (snapshot);

  node = clutter_snapshot_free_to_node (g_steal_pointer (&snapshot));
  clutter_paint_node_paint (node, paint_context);

  return clutter_texture_content_new_from_texture (COGL_TEXTURE (texture), NULL);
}

G_MODULE_EXPORT int
test_snapshot_main (int argc, char *argv[])
{
  g_autoptr (ClutterContent) texture_content = NULL;
  ClutterActor *stage, *actor;

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_actor_set_name (stage, "Stage");
  clutter_stage_set_title (CLUTTER_STAGE (stage), "ClutterSnapshot");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);
  clutter_actor_show (stage);

  texture_content = create_content (800, 600);
  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, &COGL_COLOR_INIT (0x00, 0x00, 0xff, 0x00));
  clutter_actor_set_content (actor, texture_content);
  clutter_actor_set_request_mode (actor, CLUTTER_REQUEST_CONTENT_SIZE);
  clutter_actor_add_child (stage, actor);

  clutter_test_main ();

  return EXIT_SUCCESS;
}
const char * test_snapshot_describe (void);

G_MODULE_EXPORT const char *
test_snapshot_describe (void)
{
  return "Interactive test for ClutterSnapshot";
}
