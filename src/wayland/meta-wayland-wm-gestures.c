/*
 * Wayland Support
 *
 * Copyright (C) 2023 Red Hat, Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "wayland/meta-wayland-wm-gestures.h"

#include "core/window-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-xdg-shell.h"

#include "wm-gestures-v1-server-protocol.h"

static GQuark quark_xdg_gestures_data = 0;

typedef struct _MetaWaylandXdgGestures MetaWaylandXdgGestures;

struct _MetaWaylandXdgWmGestures
{
  GObject parent;
  MetaWaylandCompositor *compositor;
  struct wl_list resources;
};

G_DEFINE_TYPE (MetaWaylandXdgWmGestures, meta_wayland_xdg_wm_gestures, G_TYPE_OBJECT)

static void
xdg_wm_gestures_destroy (struct wl_client   *client,
                         struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_wm_gestures_action (struct wl_client   *client,
                        struct wl_resource *resource,
                        uint32_t            serial,
                        struct wl_resource *seat_resource,
                        struct wl_resource *toplevel_resource,
                        uint32_t            gesture_action,
                        uint32_t            location)
{
  GDesktopTitlebarAction action = G_DESKTOP_TITLEBAR_ACTION_NONE;
  MetaWaylandSeat *seat;
  MetaWaylandXdgSurface *xdg_surface;
  MetaWaylandSurface *surface;
  MetaWindow *window;
  float x, y;

  seat = wl_resource_get_user_data (seat_resource);
  xdg_surface = wl_resource_get_user_data (toplevel_resource);
  surface = meta_wayland_surface_role_get_surface (
    META_WAYLAND_SURFACE_ROLE (xdg_surface));

  if (!meta_wayland_seat_get_grab_info (seat, surface, serial, FALSE,
                                        NULL, NULL, &x, &y))
    return;

  if (location == XDG_WM_GESTURES_V1_LOCATION_CONTENT)
    return;

  window = meta_wayland_surface_get_window (surface);

  switch (gesture_action)
    {
    case XDG_WM_GESTURES_V1_ACTION_DOUBLE_CLICK:
      action = meta_prefs_get_action_double_click_titlebar ();
      break;
    case XDG_WM_GESTURES_V1_ACTION_RIGHT_CLICK:
      action = meta_prefs_get_action_right_click_titlebar ();
      break;
    case XDG_WM_GESTURES_V1_ACTION_MIDDLE_CLICK:
      action = meta_prefs_get_action_middle_click_titlebar ();
      break;
    default:
      wl_resource_post_error (resource,
                              XDG_WM_GESTURES_V1_ERROR_INVALID_ACTION,
                              "Invalid action passed");
      break;
    }

  switch (action)
    {
    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE:
      if (!window->has_maximize_func)
        break;

      if (META_WINDOW_MAXIMIZED (window))
        meta_window_unmaximize (window, META_MAXIMIZE_BOTH);
      else
        meta_window_maximize (window, META_MAXIMIZE_BOTH);
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_HORIZONTALLY:
      if (!window->has_maximize_func)
        break;

      if (META_WINDOW_MAXIMIZED_HORIZONTALLY (window))
        meta_window_unmaximize (window, META_MAXIMIZE_HORIZONTAL);
      else
        meta_window_maximize (window, META_MAXIMIZE_HORIZONTAL);
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_VERTICALLY:
      if (!window->has_maximize_func)
        break;

      if (META_WINDOW_MAXIMIZED_VERTICALLY (window))
        meta_window_unmaximize (window, META_MAXIMIZE_VERTICAL);
      else
        meta_window_maximize (window, META_MAXIMIZE_VERTICAL);
      break;

    case G_DESKTOP_TITLEBAR_ACTION_MINIMIZE:
      if (!window->has_minimize_func)
        break;

      meta_window_minimize (window);
      break;

    case G_DESKTOP_TITLEBAR_ACTION_LOWER:
      {
        uint32_t timestamp;

        timestamp = meta_display_get_current_time_roundtrip (window->display);
        meta_window_lower_with_transients (window, timestamp);
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_MENU:
      meta_window_show_menu (window, META_WINDOW_MENU_WM, x, y);
      break;

    default:
      break;
    }
}

static const struct xdg_wm_gestures_v1_interface meta_wayland_xdg_wm_gestures_interface = {
  xdg_wm_gestures_destroy,
  xdg_wm_gestures_action,
};

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
bind_xdg_wm_gestures (struct wl_client *client,
                      void             *data,
                      uint32_t          version,
                      uint32_t          id)
{
  MetaWaylandXdgWmGestures *xdg_wm_gestures = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &xdg_wm_gestures_v1_interface, version, id);
  wl_resource_set_implementation (resource, &meta_wayland_xdg_wm_gestures_interface,
                                  data, unbind_resource);
  wl_list_insert (&xdg_wm_gestures->resources, wl_resource_get_link (resource));
}

static void
meta_wayland_xdg_wm_gestures_init (MetaWaylandXdgWmGestures *xdg_wm_gestures)
{
  wl_list_init (&xdg_wm_gestures->resources);
}

static void
meta_wayland_xdg_wm_gestures_class_init (MetaWaylandXdgWmGesturesClass *klass)
{
  quark_xdg_gestures_data =
    g_quark_from_static_string ("-meta-wayland-xdg-wm-gestures-surface-data");
}

static MetaWaylandXdgWmGestures *
meta_wayland_xdg_wm_gestures_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandXdgWmGestures *xdg_wm_gestures;

  xdg_wm_gestures = g_object_new (META_TYPE_WAYLAND_XDG_WM_GESTURES, NULL);

  if (wl_global_create (compositor->wayland_display,
                        &xdg_wm_gestures_v1_interface,
                        META_XDG_WM_GESTURES_VERSION,
                        xdg_wm_gestures, bind_xdg_wm_gestures) == NULL)
    g_error ("Failed to register a global xdg-wm-gestures object");

  xdg_wm_gestures->compositor = compositor;

  return xdg_wm_gestures;
}

void
meta_wayland_init_xdg_wm_gestures (MetaWaylandCompositor *compositor)
{
  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-xdg-wm-gestures",
                          meta_wayland_xdg_wm_gestures_new (compositor),
                          g_object_unref);
}
