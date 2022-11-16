#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"
#include "test-utils.h"

static void
on_presented (ClutterStage     *stage,
              ClutterStageView *view,
              ClutterFrameInfo *frame_info,
              gboolean         *was_presented)
{
  *was_presented = TRUE;
}

static gboolean
on_captured_event (ClutterActor *actor,
                   ClutterEvent *event,
                   unsigned int *n_captured_events)
{
  (*n_captured_events)++;

  return CLUTTER_EVENT_STOP;
}

static void
wait_presented (gboolean *was_presented)
{
  *was_presented = FALSE;
  while (!*was_presented)
    g_main_context_iteration (NULL, FALSE);
}

static void
event_delivery_consecutive_touch_begin_end (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  gboolean was_presented;
  unsigned int n_captured_touch_events = 0;

  g_signal_connect (stage, "presented", G_CALLBACK (on_presented),
                    &was_presented);
  g_signal_connect (stage, "captured-event::touch", G_CALLBACK (on_captured_event),
                    &n_captured_touch_events);

  clutter_actor_show (stage);

  was_presented = FALSE;

  clutter_test_utils_queue_pointer_event (CLUTTER_STAGE (stage), CLUTTER_TOUCH_BEGIN, 0, 15, 15);
  clutter_test_utils_queue_pointer_event (CLUTTER_STAGE (stage), CLUTTER_TOUCH_END, 0, 15, 15);
  clutter_test_utils_queue_pointer_event (CLUTTER_STAGE (stage), CLUTTER_TOUCH_BEGIN, 0, 15, 20);
  g_assert_true (!was_presented);
  wait_presented (&was_presented);
  g_assert_cmpint (n_captured_touch_events, ==, 3);

  clutter_test_utils_queue_pointer_event (CLUTTER_STAGE (stage), CLUTTER_TOUCH_END, 0, 15, 15);
  wait_presented (&was_presented);
  g_assert_cmpint (n_captured_touch_events, ==, 4);

  g_signal_handlers_disconnect_by_func (stage, on_captured_event, &n_captured_touch_events);
  g_signal_handlers_disconnect_by_func (stage, on_presented, &was_presented);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/event/delivery/consecutive-touch-begin-end", event_delivery_consecutive_touch_begin_end);
)
