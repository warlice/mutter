/*
 * Copyright (C) 2018 Red Hat
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

#include "core/meta-clipboard-manager.h"
#include "core/meta-selection-private.h"
#include "meta/meta-selection-source-memory.h"

#define MAX_TEXT_SIZE (4 * 1024 * 1024) /* 4MB */
#define MAX_IMAGE_SIZE (200 * 1024 * 1024) /* 200MB */

/* Supported mimetype globs, from least to most preferred */
static struct {
  const char *mimetype_glob;
  ssize_t max_transfer_size;
} supported_mimetypes[] = {
  { "image/tiff",               MAX_IMAGE_SIZE },
  { "image/bmp",                MAX_IMAGE_SIZE },
  { "image/gif",                MAX_IMAGE_SIZE },
  { "image/jpeg",               MAX_IMAGE_SIZE },
  { "image/webp",               MAX_IMAGE_SIZE },
  { "image/png",                MAX_IMAGE_SIZE },
  { "image/svg+xml",            MAX_IMAGE_SIZE },
  { "text/plain",               MAX_TEXT_SIZE },
  { "text/plain;charset=utf-8", MAX_TEXT_SIZE },
};

static gboolean
mimetype_match (const char *mimetype,
                int        *idx,
                gssize     *max_transfer_size)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (supported_mimetypes); i++)
    {
      if (g_pattern_match_simple (supported_mimetypes[i].mimetype_glob, mimetype))
        {
          *max_transfer_size = supported_mimetypes[i].max_transfer_size;
          *idx = i;
          return TRUE;
        }
    }

  return FALSE;
}

static void
transfer_cb (MetaSelection *selection,
             GAsyncResult  *result,
             GOutputStream *output_stream)
{
  MetaDisplay *display = meta_selection_get_display (selection);
  g_autoptr (GOutputStream) output = output_stream;
  g_autoptr (GError) error = NULL;

  if (!meta_selection_transfer_finish (selection, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to store clipboard: %s", error->message);

      return;
    }

  g_output_stream_close (output, NULL, NULL);
  display->saved_clipboard =
    g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
}

static void
owner_changed_cb (MetaSelection       *selection,
                  MetaSelectionType    selection_type,
                  MetaSelectionSource *new_owner,
                  MetaDisplay         *display)
{
  if (selection_type != META_SELECTION_CLIPBOARD)
    return;

  if (new_owner && new_owner != display->selection_source)
    {
      GOutputStream *output;
      GList *mimetypes, *l;
      int best_idx = -1;
      const char *best = NULL;
      ssize_t transfer_size = -1;

      /* New selection source, find the best mimetype in order to
       * keep a copy of it.
       */
      g_cancellable_cancel (display->saved_clipboard_cancellable);
      g_clear_object (&display->saved_clipboard_cancellable);
      g_clear_object (&display->selection_source);
      g_clear_pointer (&display->saved_clipboard_mimetype, g_free);
      g_clear_pointer (&display->saved_clipboard, g_bytes_unref);

      mimetypes = meta_selection_get_mimetypes (selection, selection_type);

      for (l = mimetypes; l; l = l->next)
        {
          gssize max_transfer_size;
          int idx;

          if (!mimetype_match (l->data, &idx, &max_transfer_size))
            continue;

          if (best_idx < idx)
            {
              best_idx = idx;
              best = l->data;
              transfer_size = max_transfer_size;
            }
        }

      if (!best)
        {
          g_list_free_full (mimetypes, g_free);
          return;
        }

      display->saved_clipboard_mimetype = g_strdup (best);
      g_list_free_full (mimetypes, g_free);
      output = g_memory_output_stream_new_resizable ();
      display->saved_clipboard_cancellable = g_cancellable_new ();
      meta_selection_transfer_async (selection,
                                     META_SELECTION_CLIPBOARD,
                                     display->saved_clipboard_mimetype,
                                     transfer_size,
                                     output,
                                     display->saved_clipboard_cancellable,
                                     (GAsyncReadyCallback) transfer_cb,
                                     output);
    }
  else if (!new_owner && display->saved_clipboard && display->saved_clipboard_mimetype)
    {
      g_autoptr (GError) error = NULL;
      g_autoptr (MetaSelectionSource) new_source = NULL;

      /* Old owner is gone, time to take over */
      new_source = meta_selection_source_memory_new (display->saved_clipboard_mimetype,
                                                     display->saved_clipboard,
                                                     &error);
      if (!new_source)
        {
          g_warning ("MetaClipboardManager failed to create new MetaSelectionSourceMemory: %s",
                     error->message);
          return;
        }

      g_set_object (&display->selection_source, new_source);
      meta_selection_set_owner (selection, selection_type, new_source);
    }
}

void
meta_clipboard_manager_init (MetaDisplay *display)
{
  MetaSelection *selection;

  selection = meta_display_get_selection (display);
  g_signal_connect_after (selection, "owner-changed",
                          G_CALLBACK (owner_changed_cb), display);
}

void
meta_clipboard_manager_shutdown (MetaDisplay *display)
{
  MetaSelection *selection;

  g_cancellable_cancel (display->saved_clipboard_cancellable);
  g_clear_object (&display->saved_clipboard_cancellable);
  g_clear_object (&display->selection_source);
  g_clear_pointer (&display->saved_clipboard, g_bytes_unref);
  g_clear_pointer (&display->saved_clipboard_mimetype, g_free);
  selection = meta_display_get_selection (display);
  g_signal_handlers_disconnect_by_func (selection, owner_changed_cb, display);
}
