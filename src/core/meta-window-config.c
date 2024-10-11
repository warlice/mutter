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

#include "meta/meta-window-config.h"

/**
 * MetaWindowConfig:
 *
 * An object representing the configuration of a top-level window
 *
 */

struct _MetaWindowConfig {
  GObject parent;

  /* The window geometry */
  MtkRectangle *rect;
  gboolean is_fullscreen;
};

G_DEFINE_TYPE (MetaWindowConfig, meta_window_config, G_TYPE_OBJECT)

static void
meta_window_config_class_init (MetaWindowConfigClass *klass)
{
}

static void
meta_window_config_init (MetaWindowConfig *window_config)
{
}

void
meta_window_config_set_rect (MetaWindowConfig *window_config,
                             MtkRectangle     *rect)
{
    window_config->rect = rect;
}

MtkRectangle *
meta_window_config_get_rect (MetaWindowConfig *window_config)
{
    return window_config->rect;
}

void
meta_window_config_set_is_fullscreen (MetaWindowConfig *window_config,
                                      gboolean          is_fullscreen)
{
    window_config->is_fullscreen = is_fullscreen;
}

gboolean
meta_window_config_get_is_fullscreen (MetaWindowConfig *window_config)
{
    return (window_config->is_fullscreen);
}

MetaWindowConfig *
meta_window_config_new (MtkRectangle *rect,
                        gboolean      is_fullscreen)
{
  MetaWindowConfig *window_config;

  window_config = g_object_new (META_TYPE_WINDOW_CONFIG, NULL);

  meta_window_config_set_rect (window_config, rect);
  meta_window_config_set_is_fullscreen (window_config, is_fullscreen);

  return window_config;
}
