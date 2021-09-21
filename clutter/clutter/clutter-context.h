/*
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef CLUTTER_CONTEXT_H
#define CLUTTER_CONTEXT_H

#include "clutter-backend.h"
#include "clutter-stage-manager.h"
#include "clutter-settings.h"
#include "cogl-pango/cogl-pango.h"

typedef enum _ClutterContextFlags
{
  CLUTTER_CONTEXT_FLAG_NONE = 0,
  CLUTTER_CONTEXT_FLAG_NO_A11Y = 1 << 0,
} ClutterContextFlags;

struct _ClutterContext
{
  GObject parent;

  ClutterBackend *backend;
  ClutterStageManager *stage_manager;

  GAsyncQueue *events_queue;

  /* the event filters added via clutter_event_add_filter. these are
   * ordered from least recently added to most recently added */
  GList *event_filters;

  CoglPangoFontMap *font_map;

  GSList *current_event;

  GList *repaint_funcs;
  guint last_repaint_id;

  ClutterSettings *settings;

  gboolean is_initialized;
  gboolean show_fps;
};

typedef ClutterBackend * (* ClutterBackendConstructor) (gpointer user_data);

#define CLUTTER_TYPE_CONTEXT (clutter_context_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterContext, clutter_context,
                      CLUTTER, CONTEXT, GObject)

/**
 * clutter_context_new: (skip)
 */
ClutterContext * clutter_context_new (ClutterContextFlags         flags,
                                      ClutterBackendConstructor   backend_constructor,
                                      gpointer                    user_data,
                                      GError                    **error);

/**
 * clutter_context_destroy: (skip)
 */
CLUTTER_EXPORT
void clutter_context_destroy (ClutterContext *context);

/**
 * clutter_context_get_backend:
 *
 * Returns: (transfer none): The #ClutterBackend
 */
CLUTTER_EXPORT
ClutterBackend * clutter_context_get_backend (ClutterContext *context);

/**
 * clutter_context_get_settings:
 *
 * Returns: (transfer none): The #ClutterSettings
 */
CLUTTER_EXPORT
ClutterSettings * clutter_context_get_settings (ClutterContext *context);

/**
 * clutter_context_get_pango_fontmap: (skip)
 */
CoglPangoFontMap * clutter_context_get_pango_fontmap (ClutterContext *context);

ClutterTextDirection clutter_context_get_text_direction (ClutterContext *context);

#endif /* CLUTTER_CONTEXT_H */
