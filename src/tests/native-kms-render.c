/*
 * Copyright (C) 2021 Red Hat Inc.
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
 */

#include "config.h"

#include <xf86drmMode.h>

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-frame-native.h"
#include "backends/native/meta-onscreen-native.h"
#include "backends/native/meta-renderer-native-private.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device-atomic.h"
#include "core/display-private.h"
#include "meta/meta-backend.h"
#include "meta-test/meta-context-test.h"
#include "tests/drm-mock/drm-mock.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"

#define N_FRAMES_PER_TEST 30

typedef struct
{
  int number_of_frames_left;
  GMainLoop *loop;

  struct {
    int n_paints;
    int n_presentations;
    int n_direct_scanouts;
    GList *fb_ids;
  } scanout;

  gboolean wait_for_scanout;

  struct {
    int scanouts_attempted;
    gboolean scanout_sabotaged;
    gboolean fallback_painted;
    guint repaint_guard_id;
    ClutterStageView *scanout_failed_view;
  } scanout_fallback;
} KmsRenderingTest;

static MetaContext *test_context;

static gboolean
is_atomic_mode_setting (MetaKmsDevice *kms_device)
{
  MetaKmsImplDevice *kms_impl_device;

  kms_impl_device = meta_kms_device_get_impl_device (kms_device);

  return META_IS_KMS_IMPL_DEVICE_ATOMIC (kms_impl_device);
}

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *stage_view,
                 ClutterFrame     *frame,
                 KmsRenderingTest *test)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);

  g_assert_true (meta_renderer_native_has_pending_mode_sets (renderer_native) ||
            !meta_frame_native_has_kms_update (frame_native));

  test->number_of_frames_left--;
  if (test->number_of_frames_left == 0)
    g_main_loop_quit (test->loop);
  else
    clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

static void
meta_test_kms_render_basic (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  KmsRenderingTest test;
  gulong handler_id;

  test = (KmsRenderingTest) {
    .number_of_frames_left = N_FRAMES_PER_TEST,
    .loop = g_main_loop_new (NULL, FALSE),
  };
  handler_id = g_signal_connect (stage, "after-update",
                                 G_CALLBACK (on_after_update), &test);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
  g_main_loop_run (test.loop);
  g_main_loop_unref (test.loop);

  g_assert_cmpint (test.number_of_frames_left, ==, 0);

  g_signal_handler_disconnect (stage, handler_id);
}

static void
on_scanout_before_update (ClutterStage     *stage,
                          ClutterStageView *stage_view,
                          ClutterFrame     *frame,
                          KmsRenderingTest *test)
{
  test->scanout.n_paints = 0;
}

static void
on_scanout_before_paint (ClutterStage     *stage,
                         ClutterStageView *stage_view,
                         ClutterFrame     *frame,
                         KmsRenderingTest *test)
{
  CoglScanout *scanout;
  CoglScanoutBuffer *scanout_buffer;
  MetaDrmBuffer *buffer;
  uint32_t fb_id;

  scanout = clutter_stage_view_peek_scanout (stage_view);
  if (!scanout)
    return;

  scanout_buffer = cogl_scanout_get_buffer (scanout);
  g_assert_true (META_IS_DRM_BUFFER (scanout_buffer));
  buffer = META_DRM_BUFFER (scanout_buffer);
  fb_id = meta_drm_buffer_get_fb_id (buffer);
  g_assert_cmpuint (fb_id, >, 0);
  test->scanout.fb_ids = g_list_append (test->scanout.fb_ids,
                                        GUINT_TO_POINTER (fb_id));

  /* Triple buffering, but no higher */
  g_assert_cmpuint (g_list_length (test->scanout.fb_ids), <=, 2);
}

static void
on_scanout_paint_view (ClutterStage     *stage,
                       ClutterStageView *stage_view,
                       MtkRegion        *region,
                       ClutterFrame     *frame,
                       KmsRenderingTest *test)
{
  test->scanout.n_paints++;
}

static void
on_scanout_presented (ClutterStage     *stage,
                      ClutterStageView *stage_view,
                      ClutterFrameInfo *frame_info,
                      KmsRenderingTest *test)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaDevicePool *device_pool;
  CoglFramebuffer *fb;
  MetaCrtc *crtc;
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaDeviceFile *device_file;
  GError *error = NULL;
  drmModeCrtc *drm_crtc;
  uint32_t first_fb_id_expected;

  if (test->wait_for_scanout && test->scanout.fb_ids == NULL)
    return;

  test->scanout.n_presentations++;

  device_pool = meta_backend_native_get_device_pool (backend_native);

  fb = clutter_stage_view_get_onscreen (stage_view);
  crtc = meta_onscreen_native_get_crtc (META_ONSCREEN_NATIVE (fb));
  kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (crtc));
  kms_device = meta_kms_crtc_get_device (kms_crtc);

  device_file = meta_device_pool_open (device_pool,
                                       meta_kms_device_get_path (kms_device),
                                       META_DEVICE_FILE_FLAG_TAKE_CONTROL,
                                       &error);
  if (!device_file)
    g_error ("Failed to open KMS device: %s", error->message);

  drm_crtc = drmModeGetCrtc (meta_device_file_get_fd (device_file),
                             meta_kms_crtc_get_id (kms_crtc));
  g_assert_nonnull (drm_crtc);

  if (test->scanout.fb_ids)
    {
      test->scanout.n_direct_scanouts++;
      first_fb_id_expected = GPOINTER_TO_UINT (test->scanout.fb_ids->data);
      test->scanout.fb_ids = g_list_delete_link (test->scanout.fb_ids,
                                                 test->scanout.fb_ids);
    }
  else
    {
      first_fb_id_expected = 0;
    }

  /* The buffer ID won't match on the first frame because switching from
   * triple buffered compositing to double buffered direct scanout takes
   * an extra frame to drain the queue. Thereafter we are in direct scanout
   * mode and expect the buffer IDs to match.
   */
  if (test->scanout.n_presentations > 1)
    {
      if (first_fb_id_expected == 0)
        g_assert_cmpuint (drm_crtc->buffer_id, !=, first_fb_id_expected);
      else
        g_assert_cmpuint (drm_crtc->buffer_id, ==, first_fb_id_expected);
    }

  drmModeFreeCrtc (drm_crtc);

  meta_device_file_release (device_file);

  test->number_of_frames_left--;
  if (test->number_of_frames_left <= 0)
    g_main_loop_quit (test->loop);
  else
    clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

typedef enum
{
  SCANOUT_WINDOW_STATE_NONE,
  SCANOUT_WINDOW_STATE_FULLSCREEN,
} ScanoutWindowState;

static void
meta_test_kms_render_client_scanout (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (test_context);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaKmsDevice *kms_device = meta_kms_get_devices (kms)->data;
  KmsRenderingTest test;
  MetaWaylandTestClient *wayland_test_client;
  g_autoptr (MetaWaylandTestDriver) test_driver = NULL;
  gulong before_update_handler_id;
  gulong before_paint_handler_id;
  gulong paint_view_handler_id;
  gulong presented_handler_id;
  MetaWindow *window;
  MtkRectangle view_rect;
  MtkRectangle buffer_rect;

  test_driver = meta_wayland_test_driver_new (wayland_compositor);
  meta_wayland_test_driver_set_property (test_driver,
                                         "gpu-path",
                                         meta_kms_device_get_path (kms_device));

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "dma-buf-scanout");
  g_assert_nonnull (wayland_test_client);

  test = (KmsRenderingTest) {
    .number_of_frames_left = N_FRAMES_PER_TEST,
    .loop = g_main_loop_new (NULL, FALSE),
    .scanout = {0},
    .wait_for_scanout = TRUE,
  };

  g_assert_cmpuint (g_list_length (clutter_stage_peek_stage_views (stage)),
                    ==,
                    1);
  clutter_stage_view_get_layout (clutter_stage_peek_stage_views (stage)->data,
                                 &view_rect);

  paint_view_handler_id =
    g_signal_connect (stage, "paint-view",
                      G_CALLBACK (on_scanout_paint_view), &test);
  before_update_handler_id =
    g_signal_connect (stage, "before-update",
                      G_CALLBACK (on_scanout_before_update), &test);
  before_paint_handler_id =
    g_signal_connect (stage, "before-paint",
                      G_CALLBACK (on_scanout_before_paint), &test);
  presented_handler_id =
    g_signal_connect (stage, "presented",
                      G_CALLBACK (on_scanout_presented), &test);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
  g_main_loop_run (test.loop);

  g_assert_cmpint (test.scanout.n_presentations, ==, N_FRAMES_PER_TEST);
  g_assert_cmpint (test.scanout.n_direct_scanouts, ==, N_FRAMES_PER_TEST);

  g_debug ("Unmake fullscreen");
  window = meta_find_window_from_title (test_context, "dma-buf-scanout-test");
  g_assert_true (meta_window_is_fullscreen (window));
  meta_window_unmake_fullscreen (window);

  g_debug ("Wait for fullscreen");
  meta_wayland_test_driver_wait_for_sync_point (test_driver,
                                                SCANOUT_WINDOW_STATE_NONE);
  g_assert_false (meta_window_is_fullscreen (window));

  g_debug ("Moving to 10, 10");
  meta_window_move_frame (window, TRUE, 10, 10);

  meta_window_get_buffer_rect (window, &buffer_rect);
  g_assert_cmpint (buffer_rect.width, ==, view_rect.width);
  g_assert_cmpint (buffer_rect.height, ==, view_rect.height);
  g_assert_cmpint (buffer_rect.x, ==, 10);
  g_assert_cmpint (buffer_rect.y, ==, 10);

  test.wait_for_scanout = FALSE;
  test.number_of_frames_left = N_FRAMES_PER_TEST;
  test.scanout.n_presentations = 0;
  test.scanout.n_direct_scanouts = 0;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
  g_main_loop_run (test.loop);

  g_assert_cmpint (test.scanout.n_presentations, ==, N_FRAMES_PER_TEST);
  g_assert_cmpint (test.scanout.n_direct_scanouts, ==, 0);

  g_debug ("Moving back to 0, 0");
  meta_window_move_frame (window, TRUE, 0, 0);

  meta_window_get_buffer_rect (window, &buffer_rect);
  g_assert_cmpint (buffer_rect.width, ==, view_rect.width);
  g_assert_cmpint (buffer_rect.height, ==, view_rect.height);
  g_assert_cmpint (buffer_rect.x, ==, 0);
  g_assert_cmpint (buffer_rect.y, ==, 0);

  test.wait_for_scanout = TRUE;
  test.number_of_frames_left = N_FRAMES_PER_TEST;
  test.scanout.n_presentations = 0;
  test.scanout.n_direct_scanouts = 0;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
  g_main_loop_run (test.loop);

  g_assert_cmpint (test.scanout.n_presentations, ==, N_FRAMES_PER_TEST);
  g_assert_cmpint (test.scanout.n_direct_scanouts, ==, N_FRAMES_PER_TEST);

  g_signal_handler_disconnect (stage, before_update_handler_id);
  g_signal_handler_disconnect (stage, before_paint_handler_id);
  g_signal_handler_disconnect (stage, paint_view_handler_id);
  g_signal_handler_disconnect (stage, presented_handler_id);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
  g_main_loop_unref (test.loop);
}

static gboolean
needs_repainted_guard (gpointer user_data)
{
  g_assert_not_reached ();
  return G_SOURCE_REMOVE;
}

static void
scanout_fallback_result_feedback (const MetaKmsFeedback *kms_feedback,
                                  gpointer               user_data)
{
  KmsRenderingTest *test = user_data;

  g_assert_cmpuint (test->scanout_fallback.repaint_guard_id, ==, 0);
  g_assert_nonnull (test->scanout_fallback.scanout_failed_view);

  test->scanout_fallback.repaint_guard_id =
    g_idle_add_full (G_PRIORITY_LOW, needs_repainted_guard, test, NULL);
}

static const MetaKmsResultListenerVtable scanout_fallback_result_listener_vtable = {
  .feedback = scanout_fallback_result_feedback,
};

static void
on_scanout_fallback_before_paint (ClutterStage     *stage,
                                  ClutterStageView *stage_view,
                                  ClutterFrame     *frame,
                                  KmsRenderingTest *test)
{
  MetaRendererView *view = META_RENDERER_VIEW (stage_view);
  MetaCrtc *crtc = meta_renderer_view_get_crtc (view);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (crtc));
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);
  CoglScanout *scanout;
  MetaKmsUpdate *kms_update;

  scanout = clutter_stage_view_peek_scanout (stage_view);
  if (!scanout)
    return;

  test->scanout_fallback.scanouts_attempted++;

  /* The first scanout candidate frame will get composited due to triple
   * buffering draining the queue to drop to double buffering. So don't
   * sabotage that first frame.
   */
  if (test->scanout_fallback.scanouts_attempted < 2)
    return;

  g_assert_false (test->scanout_fallback.scanout_sabotaged);

  if (is_atomic_mode_setting (kms_device))
    {
      drm_mock_queue_error (DRM_MOCK_CALL_ATOMIC_COMMIT, EINVAL);
    }
  else
    {
      drm_mock_queue_error (DRM_MOCK_CALL_PAGE_FLIP, EINVAL);
      drm_mock_queue_error (DRM_MOCK_CALL_SET_CRTC, EINVAL);
    }

  test->scanout_fallback.scanout_sabotaged = TRUE;

  kms_update = meta_frame_native_ensure_kms_update (frame_native, kms_device);
  meta_kms_update_add_result_listener (kms_update,
                                       &scanout_fallback_result_listener_vtable,
                                       NULL,
                                       test,
                                       NULL);

  test->scanout_fallback.scanout_failed_view = stage_view;
}

static void
on_scanout_fallback_paint_view (ClutterStage     *stage,
                                ClutterStageView *stage_view,
                                MtkRegion        *region,
                                ClutterFrame     *frame,
                                KmsRenderingTest *test)
{
  if (test->scanout_fallback.scanout_sabotaged)
    {
      g_assert_cmpuint (test->scanout_fallback.repaint_guard_id, !=, 0);
      g_clear_handle_id (&test->scanout_fallback.repaint_guard_id,
                         g_source_remove);
      test->scanout_fallback.fallback_painted = TRUE;
      test->scanout_fallback.scanout_sabotaged = FALSE;
    }
  else if (test->scanout_fallback.scanouts_attempted == 1)
    {
      /* Now that we've seen the first scanout attempt that was inhibited by
       * triple buffering, try a second frame. The second one should scanout
       * and will be sabotaged.
       */
      clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
    }
}

static void
on_scanout_fallback_presented (ClutterStage     *stage,
                               ClutterStageView *stage_view,
                               ClutterFrameInfo *frame_info,
                               KmsRenderingTest *test)
{
  if (test->scanout_fallback.fallback_painted)
    g_main_loop_quit (test->loop);

  test->number_of_frames_left--;
  g_assert_cmpint (test->number_of_frames_left, >, 0);
}

static void
meta_test_kms_render_client_scanout_fallback (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaKmsDevice *kms_device = meta_kms_get_devices (kms)->data;
  KmsRenderingTest test;
  MetaWaylandTestClient *wayland_test_client;
  g_autoptr (MetaWaylandTestDriver) test_driver = NULL;
  gulong before_paint_handler_id;
  gulong paint_view_handler_id;
  gulong presented_handler_id;

  test_driver = meta_wayland_test_driver_new (wayland_compositor);
  meta_wayland_test_driver_set_property (test_driver,
                                         "gpu-path",
                                         meta_kms_device_get_path (kms_device));

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "dma-buf-scanout");
  g_assert_nonnull (wayland_test_client);

  test = (KmsRenderingTest) {
    .number_of_frames_left = N_FRAMES_PER_TEST,
    .loop = g_main_loop_new (NULL, FALSE),
  };

  before_paint_handler_id =
    g_signal_connect (stage, "before-paint",
                      G_CALLBACK (on_scanout_fallback_before_paint), &test);
  paint_view_handler_id =
    g_signal_connect (stage, "paint-view",
                      G_CALLBACK (on_scanout_fallback_paint_view), &test);
  presented_handler_id =
    g_signal_connect (stage, "presented",
                      G_CALLBACK (on_scanout_fallback_presented), &test);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "*Direct scanout page flip failed*");

  g_main_loop_run (test.loop);
  g_main_loop_unref (test.loop);

  g_test_assert_expected_messages ();

  g_signal_handler_disconnect (stage, before_paint_handler_id);
  g_signal_handler_disconnect (stage, paint_view_handler_id);
  g_signal_handler_disconnect (stage, presented_handler_id);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
meta_test_kms_render_empty_config (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  GList *logical_monitors;
  GError *error = NULL;

  logical_monitors = meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);

  meta_monitor_manager_read_current_state (monitor_manager);
  meta_monitor_manager_apply_monitors_config (monitor_manager,
                                              NULL,
                                              META_MONITORS_CONFIG_METHOD_TEMPORARY,
                                              &error);
  g_assert_no_error (error);

  logical_monitors = meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 0);

  meta_monitor_manager_read_current_state (monitor_manager);
  meta_monitor_manager_ensure_configured (monitor_manager);

  logical_monitors = meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/render/basic",
                   meta_test_kms_render_basic);
  g_test_add_func ("/backends/native/kms/render/client-scanout",
                   meta_test_kms_render_client_scanout);
  g_test_add_func ("/backends/native/kms/render/client-scanout-fallabck",
                   meta_test_kms_render_client_scanout_fallback);
  g_test_add_func ("/backends/native/kms/render/empty-config",
                   meta_test_kms_render_empty_config);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  test_context = context;

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
