/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2021
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/meta-screen-cast-stream-src-common.h"

#include "backends/meta-cursor-tracker-private.h"
#include "core/boxes-private.h"

gboolean
meta_screen_cast_stream_src_common_is_cursor_in_stream (MetaScreenCastStreamSrc *src,
                                                        MetaRectangle           *stream_area)
{
  MetaBackend *backend = meta_screen_cast_stream_src_get_backend (src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  graphene_rect_t area_rect;
  MetaCursorSprite *cursor_sprite;

  area_rect = meta_rectangle_to_graphene_rect (stream_area);

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (cursor_sprite)
    {
      graphene_rect_t cursor_rect;

      cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                         cursor_sprite);
      return graphene_rect_intersection (&cursor_rect, &area_rect, NULL);
    }
  else
    {
      MetaCursorTracker *cursor_tracker =
        meta_backend_get_cursor_tracker (backend);
      graphene_point_t cursor_position;

      meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
      return graphene_rect_contains_point (&area_rect, &cursor_position);
    }
}

void
meta_screen_cast_stream_src_common_set_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                        struct spa_meta_cursor  *spa_meta_cursor,
                                                        graphene_point_t        *cursor_position,
                                                        float                    scale,
                                                        gboolean                *cursor_bitmap_invalid)
{
  MetaBackend *backend = meta_screen_cast_stream_src_get_backend (src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  int x, y;

  x = (int) roundf (cursor_position->x);
  y = (int) roundf (cursor_position->y);

  if (*cursor_bitmap_invalid)
    {
      MetaCursorSprite *cursor_sprite =
        meta_cursor_renderer_get_cursor (cursor_renderer);

      if (cursor_sprite)
        {
          float cursor_scale;

          cursor_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);
          scale *= cursor_scale;
          meta_screen_cast_stream_src_set_cursor_sprite_metadata (src,
                                                                  spa_meta_cursor,
                                                                  cursor_sprite,
                                                                  x, y,
                                                                  scale);
        }
      else
        {
          meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (src,
                                                                        spa_meta_cursor,
                                                                        x, y);
        }

      *cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_screen_cast_stream_src_set_cursor_position_metadata (src,
                                                                spa_meta_cursor,
                                                                x, y);
    }
}
