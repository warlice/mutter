/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat, Inc.
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

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <gdk/wayland/gdkwayland.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/sync.h>

#include "core/events.h"

#define TITLEBAR_HEIGHT 10

const char *client_id = "0";
static gboolean wayland;
GHashTable *windows;
GQuark event_source_quark;
GQuark event_handlers_quark;
GQuark can_take_focus_quark;
GQuark accept_focus_quark;
GQuark maximized_quark;
GQuark fullscreen_quark;
GQuark need_update_quark;
gboolean sync_after_lines = -1;
GMainLoop *main_loop = NULL;

typedef void (*XEventHandler) (GdkSurface *window, XEvent *event);

static void read_next_line (GDataInputStream *in);

static GdkToplevelLayout *
get_base_layout (void)
{
  GdkToplevelLayout *layout = gdk_toplevel_layout_new ();

  gdk_toplevel_layout_set_resizable (layout, TRUE);

  return layout;
}

static void
apply_layout (GdkSurface        *surface,
              GdkToplevelLayout *layout)
{
  gdk_toplevel_present (GDK_TOPLEVEL (surface), layout);
  gdk_toplevel_layout_unref (layout);

  g_object_set_qdata (G_OBJECT (surface), need_update_quark, GUINT_TO_POINTER (TRUE));
}

static void
surface_sync_visible (GdkSurface *surface)
{
  GdkToplevelLayout *layout = gdk_toplevel_layout_new ();
  gboolean maximized = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (surface), maximized_quark));
  gboolean fullscreen = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (surface), fullscreen_quark));

  g_object_set_qdata (G_OBJECT (surface), need_update_quark, GUINT_TO_POINTER (TRUE));

  gdk_toplevel_layout_set_resizable (layout, TRUE);
  gdk_toplevel_layout_set_maximized (layout, maximized);
  gdk_toplevel_layout_set_fullscreen (layout, fullscreen, NULL);
  gdk_toplevel_present (GDK_TOPLEVEL (surface), layout);
  gdk_toplevel_layout_unref (layout);
}

static void
surface_set_size (GdkSurface *surface,
                  int         width,
                  int         height)
{
  g_object_set_data (G_OBJECT (surface), "surface-width", GINT_TO_POINTER (width));
  g_object_set_data (G_OBJECT (surface), "surface-height", GINT_TO_POINTER (height));
  g_object_set_qdata (G_OBJECT (surface), need_update_quark, GUINT_TO_POINTER (TRUE));

  if (gdk_surface_get_mapped (surface))
    gdk_surface_request_layout (surface);
}

static void
ensure_wm_hints (GdkSurface *surface)
{
  GdkDisplay *display = gdk_display_get_default ();
  Display *xdisplay = gdk_x11_display_get_xdisplay (display);
  Window xwindow = gdk_x11_surface_get_xid (surface);
  XWMHints wm_hints = { 0, };
  gboolean enabled = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (surface), accept_focus_quark));

  wm_hints.flags = InputHint;
  wm_hints.input = enabled ? True : False;
  XSetWMHints (xdisplay, xwindow, &wm_hints);
}

static void
ensure_wm_take_focus (GdkSurface *surface)
{
  GdkDisplay *display = gdk_display_get_default ();
  Display *xdisplay = gdk_x11_display_get_xdisplay (display);
  Window xwindow = gdk_x11_surface_get_xid (surface);
  Atom wm_take_focus = gdk_x11_get_xatom_by_name_for_display (display, "WM_TAKE_FOCUS");
  gboolean add = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (surface), can_take_focus_quark));
  Atom *protocols = NULL;
  Atom *new_protocols;
  int n_protocols = 0;
  int i, n = 0;

  gdk_display_sync (display);
  XGetWMProtocols (xdisplay, xwindow, &protocols, &n_protocols);
  new_protocols = g_new0 (Atom, n_protocols + (add ? 1 : 0));

  for (i = 0; i < n_protocols; ++i)
    {
      if (protocols[i] != wm_take_focus)
        new_protocols[n++] = protocols[i];
    }

  if (add)
    new_protocols[n++] = wm_take_focus;

  XSetWMProtocols (xdisplay, xwindow, new_protocols, n);
  XFree (new_protocols);
  XFree (protocols);
}

static void
surface_compute_size (GdkToplevel     *toplevel,
                      GdkToplevelSize *size,
                      gpointer         user_data)
{
  int width, height;

  width = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (toplevel), "surface-width"));
  height = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (toplevel), "surface-height"));

  gdk_toplevel_size_set_size (size, width, height);
  gdk_toplevel_size_set_min_size (size, 1, 1);
  gdk_toplevel_size_set_shadow_width (size, 0, 0, 0, 0);

  /* Enforce hints on X11 surfaces */
  if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    {
      ensure_wm_hints (GDK_SURFACE (toplevel));
      ensure_wm_take_focus (GDK_SURFACE (toplevel));
    }
}

static gboolean
surface_render (GdkSurface           *toplevel,
                const cairo_region_t *region,
                gpointer              user_data)
{
  GdkCairoContext *gdk_cr;
  cairo_t *cr;

  gdk_cr = gdk_surface_create_cairo_context (GDK_SURFACE (toplevel));

  gdk_draw_context_begin_frame (GDK_DRAW_CONTEXT (gdk_cr), region);
  cr = gdk_cairo_context_cairo_create (gdk_cr);

  cairo_set_source_rgb (cr, 1, 0, 0);
  cairo_paint (cr);
  cairo_destroy (cr);

  gdk_draw_context_end_frame (GDK_DRAW_CONTEXT (gdk_cr));
  g_object_unref (gdk_cr);

  return TRUE;
}

static void
surface_layout (GdkSurface *surface,
                int         width,
                int         height,
                gpointer    user_data)
{
  if (g_object_get_qdata (G_OBJECT (surface), need_update_quark))
    {
      gdk_surface_request_layout (surface);
      g_object_set_qdata (G_OBJECT (surface), need_update_quark, GUINT_TO_POINTER (FALSE));
    }
}

static void
window_export_handle_cb (GdkToplevel *toplevel,
                         const char  *handle_str,
                         gpointer     user_data)
{
  GdkSurface *surface = user_data;

  if (!gdk_wayland_toplevel_set_transient_for_exported (GDK_TOPLEVEL (surface),
                                                        (gchar *) handle_str))
    g_print ("Fail to set transient_for exported window handle %s\n", handle_str);
  gdk_toplevel_set_modal (GDK_TOPLEVEL (surface), TRUE);
}

static GdkSurface *
lookup_window (const char *window_id)
{
  GdkSurface *window = g_hash_table_lookup (windows, window_id);
  if (!window)
    g_print ("Window %s doesn't exist\n", window_id);

  return window;
}

typedef struct {
  GSource base;
  GSource **self_ref;
  GPollFD event_poll_fd;
  Display *xdisplay;
} XClientEventSource;

static gboolean
x_event_source_prepare (GSource *source,
                        int     *timeout)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  *timeout = -1;

  return XPending (x_source->xdisplay);
}

static gboolean
x_event_source_check (GSource *source)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  return XPending (x_source->xdisplay);
}

static gboolean
x_event_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  while (XPending (x_source->xdisplay))
    {
      GHashTableIter iter;
      XEvent event;
      gpointer value;

      XNextEvent (x_source->xdisplay, &event);

      g_hash_table_iter_init (&iter, windows);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          GList *l;
          GdkSurface *window = value;
          GList *handlers =
            g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

          for (l = handlers; l; l = l->next)
            {
              XEventHandler handler = l->data;
              handler (window, &event);
            }
        }
    }

  return TRUE;
}

static void
x_event_source_finalize (GSource *source)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  *x_source->self_ref = NULL;
}

static GSourceFuncs x_event_funcs = {
  x_event_source_prepare,
  x_event_source_check,
  x_event_source_dispatch,
  x_event_source_finalize,
};

static GSource*
ensure_xsource_handler (GdkDisplay *gdkdisplay)
{
  static GSource *source = NULL;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdkdisplay);
  XClientEventSource *x_source;

  if (source)
    return g_source_ref (source);

  source = g_source_new (&x_event_funcs, sizeof (XClientEventSource));
  x_source = (XClientEventSource *) source;
  x_source->self_ref = &source;
  x_source->xdisplay = xdisplay;
  x_source->event_poll_fd.fd = ConnectionNumber (xdisplay);
  x_source->event_poll_fd.events = G_IO_IN;
  g_source_add_poll (source, &x_source->event_poll_fd);

  g_source_set_priority (source, META_PRIORITY_EVENTS - 1);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return source;
}

static gboolean
window_has_x11_event_handler (GdkSurface    *surface,
                              XEventHandler  handler)
{
  GList *handlers =
    g_object_get_qdata (G_OBJECT (surface), event_handlers_quark);

  g_return_val_if_fail (handler, FALSE);
  g_return_val_if_fail (!wayland, FALSE);

  return g_list_find (handlers, handler) != NULL;
}

static void
unref_and_maybe_destroy_gsource (GSource *source)
{
  g_source_unref (source);

  if (source->ref_count == 1)
    g_source_destroy (source);
}

static void
window_add_x11_event_handler (GdkSurface    *window,
                              XEventHandler  handler)
{
  GSource *source;
  GList *handlers =
    g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

  g_return_if_fail (!window_has_x11_event_handler (window, handler));

  source = ensure_xsource_handler (gdk_display_get_default ());
  g_object_set_qdata_full (G_OBJECT (window), event_source_quark, source,
                           (GDestroyNotify) unref_and_maybe_destroy_gsource);

  handlers = g_list_append (handlers, handler);
  g_object_set_qdata (G_OBJECT (window), event_handlers_quark, handlers);
}

static void
window_remove_x11_event_handler (GdkSurface    *window,
                                 XEventHandler  handler)
{
  GList *handlers =
    g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

  g_return_if_fail (window_has_x11_event_handler (window, handler));

  g_object_set_qdata (G_OBJECT (window), event_source_quark, NULL);

  handlers = g_list_remove (handlers, handler);
  g_object_set_qdata (G_OBJECT (window), event_handlers_quark, handlers);
}

static void
handle_take_focus (GdkSurface *surface,
                   XEvent     *xevent)
{
  GdkDisplay *display = gdk_display_get_default ();
  Atom wm_protocols =
    gdk_x11_get_xatom_by_name_for_display (display, "WM_PROTOCOLS");
  Atom wm_take_focus =
    gdk_x11_get_xatom_by_name_for_display (display, "WM_TAKE_FOCUS");

  if (xevent->xany.type != ClientMessage ||
      xevent->xany.window != gdk_x11_surface_get_xid (surface))
    return;

  if (xevent->xclient.message_type == wm_protocols &&
      xevent->xclient.data.l[0] == wm_take_focus)
    {
      XSetInputFocus (xevent->xany.display,
                      gdk_x11_surface_get_xid (surface),
                      RevertToParent,
                      xevent->xclient.data.l[1]);
    }
}

static void
process_line (const char *line)
{
  GError *error = NULL;
  int argc;
  char **argv;

  if (!g_shell_parse_argv (line, &argc, &argv, &error))
    {
      g_print ("error parsing command: %s\n", error->message);
      g_error_free (error);
      return;
    }

  if (argc < 1)
    {
      g_print ("Empty command\n");
      goto out;
    }

  if (strcmp (argv[0], "create") == 0)
    {
      int i;

      if (argc  < 2)
        {
          g_print ("usage: create <id> [override|csd]\n");
          goto out;
        }

      if (g_hash_table_lookup (windows, argv[1]))
        {
          g_print ("window %s already exists\n", argv[1]);
          goto out;
        }

      gboolean override = FALSE;
      gboolean csd = FALSE;
      for (i = 2; i < argc; i++)
        {
          if (strcmp (argv[i], "override") == 0)
            override = TRUE;
          if (strcmp (argv[i], "csd") == 0)
            csd = TRUE;
        }

      if (override && csd)
        {
          g_print ("override and csd keywords are exclusive\n");
          goto out;
        }

      GdkDisplay *display = gdk_display_get_default ();
      GdkSurface *surface = gdk_surface_new_toplevel (display);
      g_object_set_qdata (G_OBJECT (surface), accept_focus_quark, GUINT_TO_POINTER (TRUE));
      g_signal_connect (surface, "compute-size",
                        G_CALLBACK (surface_compute_size), NULL);
      g_signal_connect (surface, "render",
                        G_CALLBACK (surface_render), NULL);
      g_signal_connect (surface, "layout",
                        G_CALLBACK (surface_layout), NULL);

      g_hash_table_insert (windows, g_strdup (argv[1]), surface);

      if (csd)
        gdk_toplevel_set_decorated (GDK_TOPLEVEL (surface), FALSE);

      surface_set_size (surface, 100, 100);

      gchar *title = g_strdup_printf ("test/%s/%s", client_id, argv[1]);
      gdk_toplevel_set_title (GDK_TOPLEVEL (surface), title);
      g_free (title);

      g_object_set_qdata (G_OBJECT (surface), can_take_focus_quark,
                          GUINT_TO_POINTER (TRUE));

      if (override)
        {
          GdkDisplay *display = gdk_display_get_default ();
          Display *xdisplay = gdk_x11_display_get_xdisplay (display);
          XSetWindowAttributes attrs = { 0, };
          unsigned long mask;

          attrs.override_redirect = True;
          mask = CWOverrideRedirect;

          XChangeWindowAttributes (xdisplay,
                                   gdk_x11_surface_get_xid (surface),
                                   mask,
                                   &attrs);
        }
    }
  else if (strcmp (argv[0], "set_parent") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: set_parent <window-id> <parent-id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      GdkSurface *parent_window = lookup_window (argv[2]);
      if (!parent_window)
        {
          g_print ("unknown parent window %s\n", argv[2]);
          goto out;
        }

      gdk_toplevel_set_transient_for (GDK_TOPLEVEL (window),
                                      parent_window);
    }
  else if (strcmp (argv[0], "set_parent_exported") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: set_parent_exported <window-id> <parent-id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      GdkSurface *parent_window = lookup_window (argv[2]);
      if (!parent_window)
        {
          g_print ("unknown parent window %s\n", argv[2]);
          goto out;
        }

      if (!gdk_wayland_toplevel_export_handle (GDK_TOPLEVEL (parent_window),
                                               window_export_handle_cb,
                                               window,
                                               NULL))
        g_print ("Fail to export handle for window id %s\n", argv[2]);
    }
  else if (strcmp (argv[0], "accept_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]\n", argv[0]);
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      if (!wayland &&
          window_has_x11_event_handler (window, handle_take_focus))
        {
          g_print ("Impossible to use %s for windows accepting take focus\n",
                   argv[1]);
          goto out;
        }

      gboolean enabled = g_ascii_strcasecmp (argv[2], "true") == 0;
      g_object_set_qdata (G_OBJECT (window), accept_focus_quark,
                          GUINT_TO_POINTER (enabled));
      g_object_set_qdata (G_OBJECT (window), can_take_focus_quark,
                          GUINT_TO_POINTER (enabled));
      if (gdk_surface_get_mapped (window))
        ensure_wm_hints (window);
    }
  else if (strcmp (argv[0], "can_take_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]\n", argv[0]);
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      if (wayland)
        {
          g_print ("%s not supported under wayland\n", argv[0]);
          goto out;
        }

      if (window_has_x11_event_handler (window, handle_take_focus))
        {
          g_print ("Impossible to change %s for windows accepting take focus\n",
                   argv[1]);
          goto out;
        }

      gboolean add = g_ascii_strcasecmp(argv[2], "true") == 0;
      g_object_set_qdata (G_OBJECT (window), can_take_focus_quark,
                          GUINT_TO_POINTER (add));
      if (gdk_surface_get_mapped (window))
        ensure_wm_take_focus (window);
    }
  else if (strcmp (argv[0], "accept_take_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]\n", argv[0]);
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      if (wayland)
        {
          g_print ("%s not supported under wayland\n", argv[0]);
          goto out;
        }

      if (g_object_get_qdata (G_OBJECT (window), accept_focus_quark))
        {
          g_print ("%s not supported for input windows\n", argv[0]);
          goto out;
        }

      if (!g_object_get_qdata (G_OBJECT (window), can_take_focus_quark))
        {
          g_print ("%s not supported for windows with no WM_TAKE_FOCUS set\n",
                   argv[0]);
          goto out;
        }

      if (g_ascii_strcasecmp (argv[2], "true") == 0)
        window_add_x11_event_handler (window, handle_take_focus);
      else
        window_remove_x11_event_handler (window, handle_take_focus);
    }
  else if (strcmp (argv[0], "show") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: show <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      surface_sync_visible (window);
      gdk_display_sync (gdk_display_get_default ());
    }
  else if (strcmp (argv[0], "hide") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: hide <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_surface_hide (window);
    }
  else if (strcmp (argv[0], "activate") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: activate <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_toplevel_focus (GDK_TOPLEVEL (window), GDK_CURRENT_TIME);
    }
  else if (strcmp (argv[0], "resize") == 0)
    {
      if (argc != 4)
        {
          g_print ("usage: resize <id> <width> <height>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      int width = atoi (argv[2]);
      int height = atoi (argv[3]);
      surface_set_size (window, width, height);
    }
  else if (strcmp (argv[0], "raise") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: raise <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      GdkDisplay *display = gdk_display_get_default ();
      Display *xdisplay = gdk_x11_display_get_xdisplay (display);
      XRaiseWindow (xdisplay, gdk_x11_surface_get_xid (window));
    }
  else if (strcmp (argv[0], "lower") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: lower <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_toplevel_lower (GDK_TOPLEVEL (window));
    }
  else if (strcmp (argv[0], "destroy") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: destroy <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      g_hash_table_remove (windows, argv[1]);
    }
  else if (strcmp (argv[0], "destroy_all") == 0)
    {
      if (argc != 1)
        {
          g_print ("usage: destroy_all\n");
          goto out;
        }

      g_hash_table_remove_all (windows);
    }
  else if (strcmp (argv[0], "sync") == 0)
    {
      if (argc != 1)
        {
          g_print ("usage: sync\n");
          goto out;
        }

      gdk_display_sync (gdk_display_get_default ());
    }
  else if (strcmp (argv[0], "set_counter") == 0)
    {
      XSyncCounter counter;
      int value;

      if (argc != 3)
        {
          g_print ("usage: set_counter <counter> <value>\n");
          goto out;
        }

      if (wayland)
        {
          g_print ("usage: set_counter can only be used for X11\n");
          goto out;
        }

      counter = strtoul(argv[1], NULL, 10);
      value = atoi(argv[2]);
      XSyncValue sync_value;
      XSyncIntToValue (&sync_value, value);

      XSyncSetCounter (gdk_x11_display_get_xdisplay (gdk_display_get_default ()),
                       counter, sync_value);
    }
  else if (strcmp (argv[0], "minimize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: minimize <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_toplevel_minimize (GDK_TOPLEVEL (window));
    }
  else if (strcmp (argv[0], "unminimize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: unminimize <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      surface_sync_visible (window);
    }
  else if (strcmp (argv[0], "maximize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: maximize <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      g_object_set_qdata (G_OBJECT (window), maximized_quark,
                          GUINT_TO_POINTER (TRUE));
      if (gdk_surface_get_mapped (window))
        {
          GdkToplevelLayout *layout = get_base_layout ();
          gdk_toplevel_layout_set_maximized (layout, TRUE);
          apply_layout (window, layout);
        }
    }
  else if (strcmp (argv[0], "unmaximize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: unmaximize <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      g_object_set_qdata (G_OBJECT (window), maximized_quark,
                          GUINT_TO_POINTER (FALSE));
      if (gdk_surface_get_mapped (window))
        {
          GdkToplevelLayout *layout = get_base_layout ();
          gdk_toplevel_layout_set_maximized (layout, FALSE);
          apply_layout (window, layout);
        }
    }
  else if (strcmp (argv[0], "fullscreen") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: fullscreen <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      g_object_set_qdata (G_OBJECT (window), fullscreen_quark,
                          GUINT_TO_POINTER (TRUE));
      if (gdk_surface_get_mapped (window))
        {
          GdkToplevelLayout *layout = get_base_layout ();
          gdk_toplevel_layout_set_fullscreen (layout, TRUE, NULL);
          apply_layout (window, layout);
        }
    }
  else if (strcmp (argv[0], "unfullscreen") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: unfullscreen <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      g_object_set_qdata (G_OBJECT (window), fullscreen_quark,
                          GUINT_TO_POINTER (FALSE));
      if (gdk_surface_get_mapped (window))
        {
          GdkToplevelLayout *layout = get_base_layout ();
          gdk_toplevel_layout_set_fullscreen (layout, FALSE, NULL);
          apply_layout (window, layout);
        }
    }
  else if (strcmp (argv[0], "freeze") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: freeze <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      /* FIXME */
      /* gdk_window_freeze_updates (gtk_native_get_surface (GTK_NATIVE (window))); */
    }
  else if (strcmp (argv[0], "thaw") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: thaw <id>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      /* FIXME */
      /* gdk_window_thaw_updates (gtk_native_get_surface (GTK_NATIVE (window))); */
    }
  else if (strcmp (argv[0], "assert_size") == 0)
    {
      int expected_width;
      int expected_height;
      int width;
      int height;

      if (argc != 4)
        {
          g_print ("usage: assert_size <id> <width> <height>\n");
          goto out;
        }

      GdkSurface *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      width = gdk_surface_get_width (window);
      height = gdk_surface_get_height (window);

      expected_width = atoi (argv[2]);
      expected_height = atoi (argv[3]);
      if (expected_width != width || expected_height != height)
        {
          g_print ("Expected size %dx%d didn't match actual size %dx%d\n",
                   expected_width, expected_height,
                   width, height);
          goto out;
        }
    }
  else if (strcmp (argv[0], "stop_after_next") == 0)
    {
      if (sync_after_lines != -1)
        {
          g_print ("Can't invoke 'stop_after_next' while already stopped");
          goto out;
        }

      sync_after_lines = 1;
    }
  else if (strcmp (argv[0], "continue") == 0)
    {
      if (sync_after_lines != 0)
        {
          g_print ("Can only invoke 'continue' while stopped");
          goto out;
        }

      sync_after_lines = -1;
    }
  else if (strcmp (argv[0], "clipboard-set") == 0)
    {
      GdkDisplay *display = gdk_display_get_default ();
      GdkClipboard *clipboard;
      GdkContentProvider *provider;
      GBytes *content;

      if (argc != 3)
        {
          g_print ("usage: clipboard-set <mimetype> <text>\n");
          goto out;
        }

      clipboard = gdk_display_get_clipboard (display);

      content = g_bytes_new (argv[2], strlen (argv[2]));
      provider = gdk_content_provider_new_for_bytes (argv[1], content);
      gdk_clipboard_set_content (clipboard, provider);
    }
  else
    {
      g_print ("Unknown command %s\n", argv[0]);
      goto out;
    }

  g_print ("OK\n");

 out:
  g_strfreev (argv);
}

static void
on_line_received (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  GDataInputStream *in = G_DATA_INPUT_STREAM (source);
  GError *error = NULL;
  gsize length;
  char *line = g_data_input_stream_read_line_finish_utf8 (in, result, &length, &error);

  if (line == NULL)
    {
      if (error != NULL)
        g_printerr ("Error reading from stdin: %s\n", error->message);
      g_main_loop_quit (main_loop);
      return;
    }

  process_line (line);
  g_free (line);
  read_next_line (in);
}

static void
read_next_line (GDataInputStream *in)
{
  while (sync_after_lines == 0)
    {
      GdkDisplay *display = gdk_display_get_default ();
      g_autoptr (GError) error = NULL;
      g_autofree char *line = NULL;
      size_t length;

      gdk_display_flush (display);

      line = g_data_input_stream_read_line (in, &length, NULL, &error);
      if (!line)
        {
          if (error)
            g_printerr ("Error reading from stdin: %s\n", error->message);
          g_main_loop_quit (main_loop);
          return;
        }

      process_line (line);
    }

  if (sync_after_lines >= 0)
    sync_after_lines--;

  g_data_input_stream_read_line_async (in, G_PRIORITY_DEFAULT, NULL,
                                       on_line_received, NULL);
}

const GOptionEntry options[] = {
  {
    "wayland", 0, 0, G_OPTION_ARG_NONE,
    &wayland,
    "Create a wayland client, not an X11 one",
    NULL
  },
  {
    "client-id", 0, 0, G_OPTION_ARG_STRING,
    &client_id,
    "Identifier used in Window titles for this client",
    "CLIENT_ID",
  },
  { NULL }
};

int
main(int    argc,
     char **argv)
{
  GOptionContext *context = g_option_context_new (NULL);
  GdkDisplay *display;
  GError *error = NULL;

  g_log_writer_default_set_use_stderr (TRUE);

  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context,
                               &argc, &argv, &error))
    {
      g_printerr ("%s", error->message);
      return 1;
    }

  if (wayland)
    gdk_set_allowed_backends ("wayland");
  else
    gdk_set_allowed_backends ("x11");

  gtk_init ();

  display = gdk_display_get_default ();
  g_assert_true (gdk_display_is_composited (display));

  windows = g_hash_table_new_full (g_str_hash, g_str_equal,
                                   g_free, (GDestroyNotify) gdk_surface_destroy);
  event_source_quark = g_quark_from_static_string ("event-source");
  event_handlers_quark = g_quark_from_static_string ("event-handlers");
  can_take_focus_quark = g_quark_from_static_string ("can-take-focus");
  accept_focus_quark = g_quark_from_static_string ("accept-focus");
  maximized_quark = g_quark_from_static_string ("maximized");
  fullscreen_quark = g_quark_from_static_string ("fullscreen");
  need_update_quark = g_quark_from_static_string ("need-update");

  GInputStream *raw_in = g_unix_input_stream_new (0, FALSE);
  GDataInputStream *in = g_data_input_stream_new (raw_in);

  read_next_line (in);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);

  return 0;
}
