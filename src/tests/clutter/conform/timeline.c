#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

/* This test runs three timelines at 6 fps with 10 frames.
 * Once the timelines are run it then checks that all of the frames
 * were hit and that the completed signal was fired. The timelines are then
 * run again but this time with a timeout source that introduces a
 * delay. This should cause some frames to be skipped. The test is run
 * again but only the completed signal is checked for.
 */

#define FRAME_COUNT 10
#define FPS         6

typedef struct _TimelineData TimelineData;

struct _TimelineData
{
  int timeline_num;

  guint frame_hit_count[FRAME_COUNT + 1];
  guint completed_count;
};

static void
timeline_data_init (TimelineData *data, int timeline_num)
{
  memset (data, 0, sizeof (TimelineData));
  data->timeline_num = timeline_num;
}

static void
timeline_complete_cb (ClutterTimeline *timeline,
                      TimelineData    *data)
{
  g_printerr ("%i: Completed\n", data->timeline_num);

  data->completed_count++;
}

static void
timeline_new_frame_cb (ClutterTimeline *timeline,
                       gint             msec,
                       TimelineData    *data)
{
  /* Calculate an approximate frame number from the duration with
     rounding */
  int frame_no = ((msec * FRAME_COUNT + (FRAME_COUNT * 1000 / FPS) / 2)
                  / (FRAME_COUNT * 1000 / FPS));

  g_printerr ("%i: Doing frame %d\n",
              data->timeline_num, frame_no);

  g_assert (frame_no >= 0 && frame_no <= FRAME_COUNT);

  data->frame_hit_count[frame_no]++;
}

static gboolean
check_timeline (ClutterTimeline *timeline,
                TimelineData    *data,
                gboolean         check_missed_frames)
{
  gboolean succeeded = TRUE;
  int i;
  int missed_frame_count = 0;
  int frame_offset;

  if (clutter_timeline_get_direction (timeline) == CLUTTER_TIMELINE_BACKWARD)
    frame_offset = 0;
  else
    frame_offset = 1;

  if (check_missed_frames)
    {
      for (i = 0; i < FRAME_COUNT; i++)
        if (data->frame_hit_count[i + frame_offset] < 1)
          missed_frame_count++;

      if (missed_frame_count)
        {
          g_printerr ("FAIL: missed %i frame%s for timeline %i\n",
                      missed_frame_count, missed_frame_count == 1 ? "" : "s",
                      data->timeline_num);
          succeeded = FALSE;
        }
    }

  if (data->completed_count != 1)
    {
      g_printerr ("FAIL: timeline %i completed %i times\n",
                  data->timeline_num, data->completed_count);
      succeeded = FALSE;
    }

  return succeeded;
}

static gboolean
timeout_cb (gpointer data G_GNUC_UNUSED)
{
  clutter_test_quit ();

  return FALSE;
}

static gboolean
delay_cb (gpointer data)
{
  /* Waste a bit of time so that it will skip frames */
  g_usleep (G_USEC_PER_SEC * 66 / 1000);

  return TRUE;
}

static gboolean
add_timeout_idle (gpointer user_data)
{
  clutter_threads_add_timeout (2000, timeout_cb, NULL);

  return G_SOURCE_REMOVE;
}

static void
timeline_base (void)
{
  ClutterActor *stage;
  ClutterTimeline *timeline_1;
  TimelineData data_1;
  ClutterTimeline *timeline_2;
  TimelineData data_2;
  guint delay_tag;

  stage = clutter_test_get_stage ();

  timeline_data_init (&data_1, 1);
  timeline_1 = clutter_timeline_new_for_actor (stage, FRAME_COUNT * 1000 / FPS);

  timeline_data_init (&data_2, 2);
  timeline_2 = clutter_timeline_new_for_actor (stage, FRAME_COUNT * 1000 / FPS);
  clutter_timeline_set_direction (timeline_2, CLUTTER_TIMELINE_BACKWARD);

  g_signal_connect (timeline_1,
                    "new-frame", G_CALLBACK (timeline_new_frame_cb),
                    &data_1);
  g_signal_connect (timeline_1,
                    "completed", G_CALLBACK (timeline_complete_cb),
                    &data_1);

  g_signal_connect (timeline_2,
                    "new-frame", G_CALLBACK (timeline_new_frame_cb),
                    &data_2);
  g_signal_connect (timeline_2,
                    "completed", G_CALLBACK (timeline_complete_cb),
                    &data_2);

  clutter_actor_show (stage);

  g_printerr ("Without delay...\n");

  clutter_timeline_start (timeline_1);
  clutter_timeline_start (timeline_2);

  g_idle_add (add_timeout_idle, NULL);

  clutter_test_main ();

  g_assert (check_timeline (timeline_1, &data_1, TRUE));
  g_assert (check_timeline (timeline_2, &data_2, TRUE));

  g_printerr ("With delay...\n");

  timeline_data_init (&data_1, 1);
  timeline_data_init (&data_2, 2);

  clutter_timeline_start (timeline_1);
  clutter_timeline_start (timeline_2);

  clutter_threads_add_timeout (2000, timeout_cb, NULL);
  delay_tag = clutter_threads_add_timeout (99, delay_cb, NULL);

  clutter_test_main ();

  g_assert (check_timeline (timeline_1, &data_1, FALSE));
  g_assert (check_timeline (timeline_2, &data_2, FALSE));

  g_object_unref (timeline_1);
  g_object_unref (timeline_2);

  g_clear_handle_id (&delay_tag, g_source_remove);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/timeline/base", timeline_base);
)
