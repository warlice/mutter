#include <clutter/clutter.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

static inline ClutterActor *
clutter_test_utils_create_texture_from_file (const char  *filename,
                                             GError     **error)
{
  g_autoptr (ClutterContent) image = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;

  pixbuf = gdk_pixbuf_new_from_file (filename, error);
  if (!pixbuf)
    return NULL;

  image = clutter_image_new ();
  if (!clutter_image_set_data (CLUTTER_IMAGE (image),
                               gdk_pixbuf_get_pixels (pixbuf),
                               gdk_pixbuf_get_has_alpha (pixbuf)
                               ? COGL_PIXEL_FORMAT_RGBA_8888
                               : COGL_PIXEL_FORMAT_RGB_888,
                               gdk_pixbuf_get_width (pixbuf),
                               gdk_pixbuf_get_height (pixbuf),
                               gdk_pixbuf_get_rowstride (pixbuf),
                               error))
    return NULL;

  return g_object_new (CLUTTER_TYPE_ACTOR,
                       "content", image,
                       NULL);
}

static void
clutter_test_utils_queue_pointer_event (ClutterStage     *stage,
                                        ClutterEventType  type,
                                        unsigned int      touch_slot,
                                        float             x,
                                        float             y)
{
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  ClutterInputDevice *pointer = clutter_seat_get_pointer (seat);
  ClutterEvent *event = clutter_event_new (type);

  clutter_event_set_coords (event, x, y);
  clutter_event_set_device (event, pointer);
  clutter_event_set_stage (event, stage);

  if (type == CLUTTER_TOUCH_BEGIN ||
      type == CLUTTER_TOUCH_UPDATE ||
      type == CLUTTER_TOUCH_END ||
      type == CLUTTER_TOUCH_CANCEL)
    event->touch.sequence = GINT_TO_POINTER (touch_slot + 1);

  /* Set the event as synthetic so that we don't try to do things like
   * accepting/rejecting sequences with the xserver.
   */
  event->any.flags |= CLUTTER_EVENT_FLAG_SYNTHETIC;

  clutter_event_put (event);
  clutter_event_free (event);
}
