/*
 * Copyright 2015, 2018 Red Hat, Inc.
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

#include "wayland/meta-cursor-sprite-wayland.h"

#include "backends/meta-cursor-tracker-private.h"

struct _MetaCursorSpriteWayland
{
  MetaCursorSprite parent;

  MetaWaylandSurface *surface;
  gboolean invalidated;
};

G_DEFINE_TYPE (MetaCursorSpriteWayland,
               meta_cursor_sprite_wayland,
               META_TYPE_CURSOR_SPRITE)

static gboolean
meta_cursor_sprite_wayland_realize_texture (MetaCursorSprite *sprite)
{
  MetaCursorSpriteWayland *sprite_wayland;

  sprite_wayland = META_CURSOR_SPRITE_WAYLAND (sprite);

  if (sprite_wayland->invalidated)
    {
      sprite_wayland->invalidated = FALSE;
      return TRUE;
    }

  return FALSE;
}

static gboolean
meta_cursor_sprite_wayland_is_animated (MetaCursorSprite *sprite)
{
  return FALSE;
}

static void
meta_cursor_sprite_wayland_invalidate (MetaCursorSprite *sprite)
{
  MetaCursorSpriteWayland *sprite_wayland;

  sprite_wayland = META_CURSOR_SPRITE_WAYLAND (sprite);
  sprite_wayland->invalidated = TRUE;
}

static ClutterColorState *
ensure_default_color_state (MetaCursorTracker *cursor_tracker)
{
  ClutterColorState *color_state;
  static GOnce quark_once = G_ONCE_INIT;

  g_once (&quark_once, (GThreadFunc) g_quark_from_static_string,
          (gpointer) "-meta-cursor-sprite-wayland-default-color-state");

  color_state = g_object_get_qdata (G_OBJECT (cursor_tracker),
                                    GPOINTER_TO_INT (quark_once.retval));
  if (!color_state)
    {
      MetaBackend *backend =
        meta_cursor_tracker_get_backend (cursor_tracker);
      ClutterContext *clutter_context =
        meta_backend_get_clutter_context (backend);
      ClutterColorManager *color_manager =
        clutter_context_get_color_manager (clutter_context);

      color_state = clutter_color_manager_get_default_color_state (color_manager);

      g_object_set_qdata_full (G_OBJECT (cursor_tracker),
                               GPOINTER_TO_INT (quark_once.retval),
                               g_object_ref (color_state),
                               g_object_unref);
    }

  return color_state;
}

MetaCursorSpriteWayland *
meta_cursor_sprite_wayland_new (MetaWaylandSurface *surface,
                                MetaCursorTracker  *cursor_tracker)
{
  MetaCursorSpriteWayland *sprite_wayland;
  ClutterColorState *color_state;

  color_state = ensure_default_color_state (cursor_tracker);

  sprite_wayland = g_object_new (META_TYPE_CURSOR_SPRITE_WAYLAND,
                                 "cursor-tracker", cursor_tracker,
                                 "color-state", color_state,
                                 NULL);
  sprite_wayland->surface = surface;

  return sprite_wayland;
}

MetaWaylandBuffer *
meta_cursor_sprite_wayland_get_buffer (MetaCursorSpriteWayland *sprite_wayland)
{
  return meta_wayland_surface_get_buffer (sprite_wayland->surface);
}

static void
meta_cursor_sprite_wayland_init (MetaCursorSpriteWayland *sprite_wayland)
{
}

static void
meta_cursor_sprite_wayland_class_init (MetaCursorSpriteWaylandClass *klass)
{
  MetaCursorSpriteClass *cursor_sprite_class = META_CURSOR_SPRITE_CLASS (klass);

  cursor_sprite_class->realize_texture =
    meta_cursor_sprite_wayland_realize_texture;
  cursor_sprite_class->invalidate =
    meta_cursor_sprite_wayland_invalidate;
  cursor_sprite_class->is_animated = meta_cursor_sprite_wayland_is_animated;
}
