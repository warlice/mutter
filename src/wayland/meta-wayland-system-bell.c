/*
 * Copyright (C) 2024 Red Hat Inc.
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

#include "wayland/meta-wayland-system-bell.h"

#include "core/bell.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland.h"

#include "system-bell-v1-server-protocol.h"

static void
system_bell_destroy (struct wl_client *client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
system_bell_ring (struct wl_client   *client,
                  struct wl_resource *resource,
                  struct wl_resource *surface_resource)
{
  MetaWaylandCompositor *compositor = wl_resource_get_user_data (resource);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);

  if (surface_resource)
    {
      MetaWaylandSurface *surface;
      MetaWindow *window;

      surface = wl_resource_get_user_data (surface_resource);
      if (!surface)
        return;

      window = meta_wayland_surface_get_window (surface);
      if (!window)
        return;

      meta_bell_notify (display, window);
    }
  else
    {
      meta_bell_notify (display, NULL);
    }
}

static const struct wp_system_bell_v1_interface system_bell_implementation =
{
  system_bell_destroy,
  system_bell_ring,
};

static void
system_bell_bind (struct wl_client *client,
                  void             *user_data,
                  uint32_t          version,
                  uint32_t          id)
{
  MetaWaylandCompositor *compositor = user_data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_system_bell_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource,
                                  &system_bell_implementation,
                                  compositor, NULL);
}

void
meta_wayland_init_system_bell (MetaWaylandCompositor *compositor)
{
  struct wl_display *wl_display =
    meta_wayland_compositor_get_wayland_display (compositor);

  if (!wl_global_create (wl_display,
                         &wp_system_bell_v1_interface,
                         META_WP_SYSTEM_BELL_V1_VERSION,
                         compositor,
                         system_bell_bind))
    g_warning ("Failed to create wp_system_bell_v1 global");
}
