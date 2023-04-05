/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X window decorations */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2005 Elijah Newren
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
 */

#include "config.h"

#include "core/frame.h"

#include "backends/x11/meta-backend-x11.h"
#include "compositor/compositor-private.h"
#include "core/bell.h"
#include "core/keybindings-private.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-props.h"

#include <X11/Xatom.h>

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-login.h>
#endif

#define META_X11_FRAMES_CLIENT MUTTER_LIBEXECDIR "/mutter-x11-frames"

#define EVENT_MASK (SubstructureRedirectMask |                     \
                    StructureNotifyMask | SubstructureNotifyMask | \
                    PropertyChangeMask | FocusChangeMask)

void
meta_window_ensure_frame (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  unsigned long data[1] = { 1 };

  meta_x11_error_trap_push (x11_display);

  XChangeProperty (x11_display->xdisplay,
                   window->xwindow,
                   x11_display->atom__MUTTER_NEEDS_FRAME,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);

  meta_x11_error_trap_pop (x11_display);
}

void
meta_window_set_frame_xwindow (MetaWindow *window,
                               Window      xframe)
{
  MetaX11Display *x11_display = window->display->x11_display;
  XSetWindowAttributes attrs;
  gulong create_serial = 0;
  MetaFrame *frame;

  if (window->frame)
    return;

  frame = g_new0 (MetaFrame, 1);

  frame->window = window;
  frame->xwindow = xframe;

  frame->rect = window->rect;
  frame->child_x = 0;
  frame->child_y = 0;
  frame->bottom_height = 0;
  frame->right_width = 0;

  frame->borders_cached = FALSE;

  meta_sync_counter_init (&frame->sync_counter, window, frame->xwindow);

  window->frame = frame;

  meta_verbose ("Frame geometry %d,%d  %dx%d",
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);

  meta_verbose ("Setting frame 0x%lx for window %s, "
                "frame geometry %d,%d  %dx%d",
                xframe, window->desc,
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);

  meta_stack_tracker_record_add (window->display->stack_tracker,
                                 frame->xwindow,
                                 create_serial);

  meta_verbose ("Frame for %s is 0x%lx", frame->window->desc, frame->xwindow);

  meta_x11_error_trap_push (x11_display);

  attrs.event_mask = EVENT_MASK;
  XChangeWindowAttributes (x11_display->xdisplay,
			   frame->xwindow, CWEventMask, &attrs);

  meta_x11_display_register_x_window (x11_display, &frame->xwindow, window);

  if (window->mapped)
    {
      window->mapped = FALSE; /* the reparent will unmap the window,
                               * we don't want to take that as a withdraw
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent", window->desc);
      window->unmaps_pending += 1;
    }

  meta_stack_tracker_record_remove (window->display->stack_tracker,
                                    window->xwindow,
                                    XNextRequest (x11_display->xdisplay));
  XReparentWindow (x11_display->xdisplay,
                   window->xwindow,
                   frame->xwindow,
                   frame->child_x,
                   frame->child_y);
  window->reparents_pending += 1;
  /* FIXME handle this error */
  meta_x11_error_trap_pop (x11_display);

  /* Ensure focus is restored after the unmap/map events triggered
   * by XReparentWindow().
   */
  if (meta_window_has_focus (window))
    window->restore_focus_on_map = TRUE;

  /* stick frame to the window */
  window->frame = frame;

  meta_window_reload_property_from_xwindow (window, frame->xwindow,
                                            x11_display->atom__NET_WM_SYNC_REQUEST_COUNTER,
                                            TRUE);
  meta_window_reload_property_from_xwindow (window, frame->xwindow,
                                            x11_display->atom__NET_WM_OPAQUE_REGION,
                                            TRUE);

  meta_x11_error_trap_push (x11_display);
  XMapWindow (x11_display->xdisplay, frame->xwindow);
  meta_x11_error_trap_pop (x11_display);

  /* Move keybindings to frame instead of window */
  meta_window_grab_keys (window);

  /* Even though the property was already set, notify
   * on it so other bits of the machinery catch up
   * on the new frame.
   */
  g_object_notify (G_OBJECT (window), "decorated");
}

void
meta_window_destroy_frame (MetaWindow *window)
{
  MetaFrame *frame;
  MetaFrameBorders borders;
  MetaX11Display *x11_display;

  if (window->frame == NULL)
    return;

  x11_display = window->display->x11_display;

  meta_verbose ("Unframing window %s", window->desc);

  frame = window->frame;

  meta_frame_calc_borders (frame, &borders);

  /* Unparent the client window; it may be destroyed,
   * thus the error trap.
   */
  meta_x11_error_trap_push (x11_display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* Keep track of unmapping it, so we
                               * can identify a withdraw initiated
                               * by the client.
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent back to root", window->desc);
      window->unmaps_pending += 1;
    }

  if (!x11_display->closing)
    {
      if (!window->unmanaging)
        {
          meta_stack_tracker_record_add (window->display->stack_tracker,
                                         window->xwindow,
                                         XNextRequest (x11_display->xdisplay));
        }

      XReparentWindow (x11_display->xdisplay,
                       window->xwindow,
                       x11_display->xroot,
                       /* Using anything other than client root window coordinates
                        * coordinates here means we'll need to ensure a configure
                        * notify event is sent; see bug 399552.
                        */
                       window->frame->rect.x + borders.invisible.left,
                       window->frame->rect.y + borders.invisible.top);
      window->reparents_pending += 1;
    }

  XDeleteProperty (x11_display->xdisplay,
                   window->xwindow,
                   x11_display->atom__MUTTER_NEEDS_FRAME);

  meta_x11_error_trap_pop (x11_display);

  /* Ensure focus is restored after the unmap/map events triggered
   * by XReparentWindow().
   */
  if (meta_window_has_focus (window))
    window->restore_focus_on_map = TRUE;

  meta_x11_display_unregister_x_window (x11_display, frame->xwindow);

  window->frame = NULL;
  if (window->frame_bounds)
    {
      cairo_region_destroy (window->frame_bounds);
      window->frame_bounds = NULL;
    }

  g_clear_pointer (&window->opaque_region, cairo_region_destroy);

  /* Move keybindings to window instead of frame */
  meta_window_grab_keys (window);

  meta_sync_counter_clear (&frame->sync_counter);

  g_free (frame);

  /* Put our state back where it should be */
  meta_window_queue (window, META_QUEUE_CALC_SHOWING);
  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}


MetaFrameFlags
meta_frame_get_flags (MetaFrame *frame)
{
  MetaFrameFlags flags;

  flags = 0;

  if (frame->window->border_only)
    {
      ; /* FIXME this may disable the _function_ as well as decor
         * in some cases, which is sort of wrong.
         */
    }
  else
    {
      flags |= META_FRAME_ALLOWS_MENU;

      if (frame->window->has_close_func)
        flags |= META_FRAME_ALLOWS_DELETE;

      if (frame->window->has_maximize_func)
        flags |= META_FRAME_ALLOWS_MAXIMIZE;

      if (frame->window->has_minimize_func)
        flags |= META_FRAME_ALLOWS_MINIMIZE;

    }

  if (META_WINDOW_ALLOWS_MOVE (frame->window))
    flags |= META_FRAME_ALLOWS_MOVE;

  if (META_WINDOW_ALLOWS_HORIZONTAL_RESIZE (frame->window))
    flags |= META_FRAME_ALLOWS_HORIZONTAL_RESIZE;

  if (META_WINDOW_ALLOWS_VERTICAL_RESIZE (frame->window))
    flags |= META_FRAME_ALLOWS_VERTICAL_RESIZE;

  if (meta_window_appears_focused (frame->window))
    flags |= META_FRAME_HAS_FOCUS;

  if (frame->window->on_all_workspaces_requested)
    flags |= META_FRAME_STUCK;

  /* FIXME: Should we have some kind of UI for windows that are just vertically
   * maximized or just horizontally maximized?
   */
  if (META_WINDOW_MAXIMIZED (frame->window))
    flags |= META_FRAME_MAXIMIZED;

  if (META_WINDOW_TILED_LEFT (frame->window))
    flags |= META_FRAME_TILED_LEFT;

  if (META_WINDOW_TILED_RIGHT (frame->window))
    flags |= META_FRAME_TILED_RIGHT;

  if (frame->window->fullscreen)
    flags |= META_FRAME_FULLSCREEN;

  if (frame->window->wm_state_above)
    flags |= META_FRAME_ABOVE;

  return flags;
}

void
meta_frame_borders_clear (MetaFrameBorders *self)
{
  self->visible.top    = self->invisible.top    = self->total.top    = 0;
  self->visible.bottom = self->invisible.bottom = self->total.bottom = 0;
  self->visible.left   = self->invisible.left   = self->total.left   = 0;
  self->visible.right  = self->invisible.right  = self->total.right  = 0;
}

static void
meta_frame_query_borders (MetaFrame        *frame,
                          MetaFrameBorders *borders)
{
  MetaWindow *window = frame->window;
  MetaX11Display *x11_display = window->display->x11_display;
  int format, res;
  Atom type;
  unsigned long nitems, bytes_after;
  unsigned char *data;

  if (!frame->xwindow)
    return;

  meta_x11_error_trap_push (x11_display);

  res = XGetWindowProperty (x11_display->xdisplay,
                            frame->xwindow,
                            x11_display->atom__GTK_FRAME_EXTENTS,
                            0, 4,
                            False, XA_CARDINAL,
                            &type, &format,
                            &nitems, &bytes_after,
                            (unsigned char **) &data);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    return;

  if (res == Success && nitems == 4)
    {
      borders->invisible = (MetaFrameBorder) {
        ((long *) data)[0],
        ((long *) data)[1],
        ((long *) data)[2],
        ((long *) data)[3],
      };
    }

  g_clear_pointer (&data, XFree);

  meta_x11_error_trap_push (x11_display);

  res = XGetWindowProperty (x11_display->xdisplay,
                            frame->xwindow,
                            x11_display->atom__MUTTER_FRAME_EXTENTS,
                            0, 4,
                            False, XA_CARDINAL,
                            &type, &format,
                            &nitems, &bytes_after,
                            (unsigned char **) &data);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    return;

  if (res == Success && nitems == 4)
    {
      borders->visible = (MetaFrameBorder) {
        ((long *) data)[0],
        ((long *) data)[1],
        ((long *) data)[2],
        ((long *) data)[3],
      };
    }

  g_clear_pointer (&data, XFree);

  borders->total = (MetaFrameBorder) {
    borders->invisible.left + frame->cached_borders.visible.left,
    borders->invisible.right + frame->cached_borders.visible.right,
    borders->invisible.top + frame->cached_borders.visible.top,
    borders->invisible.bottom + frame->cached_borders.visible.bottom,
  };
}

void
meta_frame_calc_borders (MetaFrame        *frame,
                         MetaFrameBorders *borders)
{
  /* Save on if statements and potential uninitialized values
   * in callers -- if there's no frame, then zero the borders. */
  if (frame == NULL)
    meta_frame_borders_clear (borders);
  else
    {
      if (!frame->borders_cached)
        {
          meta_frame_query_borders (frame, &frame->cached_borders);
          frame->borders_cached = TRUE;
        }

      *borders = frame->cached_borders;
    }
}

void
meta_frame_clear_cached_borders (MetaFrame *frame)
{
  frame->borders_cached = FALSE;
}

gboolean
meta_frame_sync_to_window (MetaFrame *frame,
                           gboolean   need_resize)
{
  MetaWindow *window = frame->window;
  MetaX11Display *x11_display = window->display->x11_display;

  meta_topic (META_DEBUG_GEOMETRY,
              "Syncing frame geometry %d,%d %dx%d (SE: %d,%d)",
              frame->rect.x, frame->rect.y,
              frame->rect.width, frame->rect.height,
              frame->rect.x + frame->rect.width,
              frame->rect.y + frame->rect.height);

  meta_x11_error_trap_push (x11_display);

  XMoveResizeWindow (x11_display->xdisplay,
                     frame->xwindow,
                     frame->rect.x,
                     frame->rect.y,
                     frame->rect.width,
                     frame->rect.height);

  meta_x11_error_trap_pop (x11_display);

  return need_resize;
}

cairo_region_t *
meta_frame_get_frame_bounds (MetaFrame *frame)
{
  MetaFrameBorders borders;
  cairo_region_t *bounds;

  meta_frame_calc_borders (frame, &borders);
  /* FIXME: currently just the client area, should shape closer to
   * frame border, incl. rounded corners.
   */
  bounds = cairo_region_create_rectangle (&(cairo_rectangle_int_t) {
    borders.total.left,
    borders.total.top,
    frame->rect.width - borders.total.left - borders.total.right,
    frame->rect.height - borders.total.top - borders.total.bottom,
  });

  return bounds;
}

void
meta_frame_get_mask (MetaFrame             *frame,
                     cairo_rectangle_int_t *frame_rect,
                     cairo_t               *cr)
{
  MetaFrameBorders borders;

  meta_frame_calc_borders (frame, &borders);

  cairo_rectangle (cr,
                   0, 0,
                   frame->rect.width,
                   frame->rect.height);
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_fill (cr);
}

Window
meta_frame_get_xwindow (MetaFrame *frame)
{
  return frame->xwindow;
}

gboolean
meta_frame_handle_xevent (MetaFrame *frame,
                          XEvent    *xevent)
{
  MetaWindow *window = frame->window;
  MetaX11Display *x11_display = window->display->x11_display;

  if (xevent->xany.type == PropertyNotify &&
      xevent->xproperty.state == PropertyNewValue &&
      (xevent->xproperty.atom == x11_display->atom__GTK_FRAME_EXTENTS ||
       xevent->xproperty.atom == x11_display->atom__MUTTER_FRAME_EXTENTS))
    {
      meta_window_frame_size_changed (window);
      meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
      return TRUE;
    }
  else if (xevent->xany.type == PropertyNotify &&
           xevent->xproperty.state == PropertyNewValue &&
           (xevent->xproperty.atom == x11_display->atom__NET_WM_SYNC_REQUEST_COUNTER ||
            xevent->xproperty.atom == x11_display->atom__NET_WM_OPAQUE_REGION))
    {
      meta_window_reload_property_from_xwindow (window, frame->xwindow,
                                                xevent->xproperty.atom, FALSE);
      return TRUE;
    }

  return FALSE;
}

GSubprocess *
meta_frame_launch_client (MetaX11Display *x11_display,
                          const char     *display_name)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError) error = NULL;
  GSubprocess *proc;
  const char *args[2];

  args[0] = META_X11_FRAMES_CLIENT;
  args[1] = NULL;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher, "DISPLAY", display_name, TRUE);

  proc = g_subprocess_launcher_spawnv (launcher, args, &error);
  if (error)
    {
      if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        {
          /* Fallback case for uninstalled tests, relies on CWD being
           * the builddir, as it is the case during "ninja test".
           */
          g_clear_error (&error);
          args[0] = "./src/frames/mutter-x11-frames";
          proc = g_subprocess_launcher_spawnv (launcher, args, &error);
        }

      if (error)
        {
          g_warning ("Could not launch X11 frames client: %s", error->message);
          return NULL;
        }
    }

  return proc;
}

#ifdef HAVE_LIBSYSTEMD

static void
on_start_transient_unit_called (GObject      *source,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source),
                                         res, &error);

  if (error)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
meta_frame_launch_client_systemd_transient_unit (GDBusConnection *connection,
                                                 GTask           *task)
{
  GVariantBuilder builder;
  GVariantBuilder exec_builder;
  g_autofree char *parent_unit = NULL;
  g_autofree char *unit_name = NULL;
  g_auto(GStrv) env = NULL;
  const char *display_name;

  display_name = g_task_get_task_data (task);
  unit_name = g_strdup_printf ("mutter-x11-frames@%s.service", display_name);

  for (size_t i = 0, len = strlen (display_name); i < len; ++i)
    {
      if (g_ascii_isalnum (display_name[i]) ||
          display_name[i] == '-' ||
          display_name[i] == ':' ||
          display_name[i] == '_' ||
          display_name[i] == '\\' ||
          display_name[i] == '.')
          continue;

      unit_name[i] = '_';
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(ssa(sv)a(sa(sv)))"));
  g_variant_builder_add (&builder, "s", unit_name);
  g_variant_builder_add (&builder, "s", "replace");

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(sv)"));
  g_variant_builder_add (&builder,
                         "(sv)",
                         "Type",
                         g_variant_new_string ("simple"));

  g_variant_builder_add (&builder,
                          "(sv)",
                          "Description",
                          g_variant_new_string ("Mutter X11 Frames Client"));

  g_variant_builder_add (&builder,
                          "(sv)",
                          "Slice",
                          g_variant_new_string ("session.slice"));

  if (sd_pid_get_user_unit (getpid (), &parent_unit) != 0)
    parent_unit = g_strdup ("graphical-session.target");

  g_variant_builder_add (&builder,
                         "(sv)",
                         "PartOf",
                         g_variant_new_strv (
                          (const char *const[]) { parent_unit }, 1));

  env = g_get_environ ();
  env = g_environ_setenv (env, "DISPLAY", display_name, TRUE);
  g_variant_builder_add (&builder,
                          "(sv)",
                          "Environment",
                          g_variant_new_strv ((const char *const *) env, -1));

  g_variant_builder_add (&builder,
                         "(sv)",
                         "Restart",
                          g_variant_new_string ("always"));

  g_variant_builder_add (&builder,
                         "(sv)",
                         "CollectMode",
                          g_variant_new_string ("inactive-or-failed"));

  /* The binary path to run */
  g_variant_builder_init (&exec_builder, G_VARIANT_TYPE ("a(sasb)"));
  g_variant_builder_open (&exec_builder, G_VARIANT_TYPE ("(sasb)"));
  g_variant_builder_add (&exec_builder, "s", META_X11_FRAMES_CLIENT);

  /* The argv */
  g_variant_builder_open (&exec_builder, G_VARIANT_TYPE ("as"));
  g_variant_builder_add (&exec_builder, "s", META_X11_FRAMES_CLIENT);
  g_variant_builder_close (&exec_builder);

  /* It's a failure if the process exits uncleanly */
  g_variant_builder_add (&exec_builder, "b", TRUE);

  g_variant_builder_close (&exec_builder);

  g_variant_builder_add (&builder,
                         "(sv)",
                         "ExecStart",
                          g_variant_builder_end (&exec_builder));

  g_variant_builder_close (&builder);

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(sa(sv))"));
  g_variant_builder_close (&builder);

  g_dbus_connection_call (connection,
                          "org.freedesktop.systemd1",
                          "/org/freedesktop/systemd1",
                          "org.freedesktop.systemd1.Manager",
                          "StartTransientUnit",
                          g_variant_builder_end (&builder),
                          G_VARIANT_TYPE ("(o)"),
                          G_DBUS_CALL_FLAGS_NO_AUTO_START,
                          1000,
                          g_task_get_cancellable (task),
                          on_start_transient_unit_called,
                          g_object_ref (task));
}

static void
on_session_bus_got (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GError) error = NULL;

  connection = g_bus_get_finish (result, &error);
  if (error)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  meta_frame_launch_client_systemd_transient_unit (connection, task);
}

static void
on_meta_launch_client_file_info (GObject      *source,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  GFile *file = G_FILE (source);
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GError) error = NULL;

  info = g_file_query_info_finish (file, result, &error);
  if (error)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (g_file_info_get_file_type (info) != G_FILE_TYPE_REGULAR)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                               "'%s' is not a regular file",
                               g_file_peek_path (file));
      return;
    }

  if (!g_file_info_get_attribute_boolean (info,
                                          G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                               "'%s' is not an executable file",
                               g_file_peek_path (file));
      return;
    }

  g_bus_get (G_BUS_TYPE_SESSION, g_task_get_cancellable (task),
             on_session_bus_got, g_object_ref (task));
}

#endif

void
meta_frame_launch_client_async (MetaX11Display      *x11_display,
                                const char          *display_name,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (GFile) client_binary = NULL;

  task = g_task_new (x11_display, cancellable, callback, user_data);

#ifdef HAVE_LIBSYSTEMD
  g_task_set_task_data (task, g_strdup (display_name), g_free);

  client_binary = g_file_new_for_path (META_X11_FRAMES_CLIENT);
  g_file_query_info_async (client_binary,
                           G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                           G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT, cancellable,
                           on_meta_launch_client_file_info,
                           g_object_ref (task));
#else
  g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "Systemd support is not enabled");
#endif
}

gboolean
meta_frame_launch_client_finish (MetaX11Display  *x11_display,
                                 GAsyncResult    *result,
                                 GError         **error)
{
  g_return_val_if_fail (g_task_is_valid (result, x11_display), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * meta_frame_type_to_string:
 * @type: a #MetaFrameType
 *
 * Converts a frame type enum value to the name string that would
 * appear in the theme definition file.
 *
 * Return value: the string value
 */
const char *
meta_frame_type_to_string (MetaFrameType type)
{
  switch (type)
    {
    case META_FRAME_TYPE_NORMAL:
      return "normal";
    case META_FRAME_TYPE_DIALOG:
      return "dialog";
    case META_FRAME_TYPE_MODAL_DIALOG:
      return "modal_dialog";
    case META_FRAME_TYPE_UTILITY:
      return "utility";
    case META_FRAME_TYPE_MENU:
      return "menu";
    case META_FRAME_TYPE_BORDER:
      return "border";
    case META_FRAME_TYPE_ATTACHED:
      return "attached";
    case  META_FRAME_TYPE_LAST:
      break;
    }

  return "<unknown>";
}

MetaSyncCounter *
meta_frame_get_sync_counter (MetaFrame *frame)
{
  return &frame->sync_counter;
}

void
meta_frame_set_opaque_region (MetaFrame      *frame,
                              cairo_region_t *region)
{
  MetaWindow *window = frame->window;

  if (cairo_region_equal (frame->opaque_region, region))
    return;

  g_clear_pointer (&frame->opaque_region, cairo_region_destroy);

  if (region != NULL)
    frame->opaque_region = cairo_region_reference (region);

  meta_compositor_window_shape_changed (window->display->compositor, window);
}
