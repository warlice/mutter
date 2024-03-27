/*
 * Copyright 2024 Red Hat, Inc.
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

#include "wayland/meta-wayland-cursor-shape.h"

#include "core/window-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-xdg-shell.h"
#include "wayland/meta-wayland-versions.h"

#include "cursor-shape-v1-server-protocol.h"

typedef struct _MetaWaylandCursorShapeDevice
{
  MetaWaylandPointer *pointer;
  MetaWaylandTabletTool *tool;
} MetaWaylandCursorShapeDevice;

static const struct
{
  enum wp_cursor_shape_device_v1_shape shape;
  MetaCursor cursor;
} shape_map[] = {
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CONTEXT_MENU, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS, META_CURSOR_BUSY },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT, META_CURSOR_BUSY },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR, META_CURSOR_CROSSHAIR },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALIAS, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COPY, META_CURSOR_DND_COPY },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE, META_CURSOR_DND_MOVE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP, META_CURSOR_DND_UNSUPPORTED_TARGET },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB, META_CURSOR_DND_IN_DRAG },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING, META_CURSOR_DND_IN_DRAG },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE, META_CURSOR_EAST_RESIZE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE, META_CURSOR_NORTH_RESIZE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE, META_CURSOR_NE_RESIZE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE, META_CURSOR_NW_RESIZE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE, META_CURSOR_SOUTH_RESIZE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE, META_CURSOR_SE_RESIZE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE, META_CURSOR_SW_RESIZE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE, META_CURSOR_WEST_RESIZE },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE, META_CURSOR_MOVE_OR_RESIZE_WINDOW },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE, META_CURSOR_MOVE_OR_RESIZE_WINDOW },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE, META_CURSOR_MOVE_OR_RESIZE_WINDOW },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE, META_CURSOR_MOVE_OR_RESIZE_WINDOW },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE, META_CURSOR_MOVE_OR_RESIZE_WINDOW },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE, META_CURSOR_MOVE_OR_RESIZE_WINDOW },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_IN, META_CURSOR_DEFAULT },
  { WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT, META_CURSOR_DEFAULT },
};

static MetaCursor
cursor_from_shape (enum wp_cursor_shape_device_v1_shape shape)
{
  if (shape < WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT ||
      shape > WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT)
    return META_CURSOR_NONE;

  return shape_map[shape].cursor;
}

static void
cursor_shape_device_destructor (struct wl_resource *resource)
{
  MetaWaylandCursorShapeDevice *cursor_shape_device =
    wl_resource_get_user_data (resource);

  g_clear_weak_pointer ((gpointer *) &cursor_shape_device->pointer);
  g_clear_weak_pointer ((gpointer *) &cursor_shape_device->tool);

  g_free (cursor_shape_device);
}

static void
cursor_shape_device_destroy (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
cursor_shape_device_set_shape (struct wl_client                     *client,
                               struct wl_resource                   *resource,
                               uint32_t                              serial,
                               enum wp_cursor_shape_device_v1_shape  shape)
{
  MetaWaylandCursorShapeDevice *cursor_shape_device =
    wl_resource_get_user_data (resource);
  MetaCursor cursor = cursor_from_shape (shape);

  if (cursor == META_CURSOR_NONE)
    {
      wl_resource_post_error (resource,
                              WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE,
                              "wp_cursor_shape_device_v1@%d: "
                              "the specified shape value is invalid",
                              wl_resource_get_id (resource));
      return;
    }

  if (cursor_shape_device->pointer)
    {
      meta_wayland_pointer_set_shape (cursor_shape_device->pointer,
                                      client, serial, cursor);
    }
  else if (cursor_shape_device->tool)
    {
      meta_wayland_tablet_tool_set_shape (cursor_shape_device->tool,
                                          client, serial, cursor);
    }
  else
    g_assert_not_reached ();
}

static const struct wp_cursor_shape_device_v1_interface cursor_shape_device_interface = {
  cursor_shape_device_destroy,
  cursor_shape_device_set_shape,
};

static void
cursor_manager_destroy (struct wl_client   *client,
                        struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
cursor_manager_get_pointer (struct wl_client   *client,
                            struct wl_resource *resource,
                            uint32_t            id,
                            struct wl_resource *pointer_resource)
{
  MetaWaylandPointer *pointer = wl_resource_get_user_data (pointer_resource);
  MetaWaylandCursorShapeDevice *cursor_shape_device;
  struct wl_resource *shape_device_resource;

  cursor_shape_device = g_new0 (MetaWaylandCursorShapeDevice, 1);
  cursor_shape_device->pointer = pointer;
  g_object_add_weak_pointer (G_OBJECT (pointer),
                             (gpointer *) &cursor_shape_device->pointer);

  shape_device_resource =
    wl_resource_create (client,
                        &wp_cursor_shape_device_v1_interface,
                        wl_resource_get_version (resource),
                        id);
  wl_resource_set_implementation (shape_device_resource,
                                  &cursor_shape_device_interface,
                                  cursor_shape_device,
                                  cursor_shape_device_destructor);
}

static void
cursor_manager_get_tablet_tool_v2 (struct wl_client   *client,
                                   struct wl_resource *resource,
                                   uint32_t            id,
                                   struct wl_resource *pointer_resource)
{
  MetaWaylandTabletTool *tool = wl_resource_get_user_data (resource);
  MetaWaylandCursorShapeDevice *cursor_shape_device;
  struct wl_resource *shape_device_resource;

  cursor_shape_device = g_new0 (MetaWaylandCursorShapeDevice, 1);
  cursor_shape_device->tool = tool;
  g_object_add_weak_pointer (G_OBJECT (tool),
                             (gpointer *) &cursor_shape_device->tool);

  shape_device_resource =
    wl_resource_create (client,
                        &wp_cursor_shape_device_v1_interface,
                        wl_resource_get_version (resource),
                        id);
  wl_resource_set_implementation (shape_device_resource,
                                  &cursor_shape_device_interface,
                                  cursor_shape_device,
                                  cursor_shape_device_destructor);
}

static const struct wp_cursor_shape_manager_v1_interface cursor_shape_manager_interface = {
  cursor_manager_destroy,
  cursor_manager_get_pointer,
  cursor_manager_get_tablet_tool_v2,
};

static void
bind_cursor_shape (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wp_cursor_shape_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &cursor_shape_manager_interface,
                                  NULL, NULL);
}

void
meta_wayland_init_cursor_shape (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &wp_cursor_shape_manager_v1_interface,
                        META_WP_CURSOR_SHAPE_VERSION,
                        NULL, bind_cursor_shape) == NULL)
    g_error ("Failed to register a global cursor-shape object");
}
