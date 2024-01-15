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

#include "meta/meta-selection-source-memory.h"

struct _MetaSelectionSourceMemory
{
  MetaSelectionSource parent_instance;
  GList *mimetypes;
  GList *values;
};

G_DEFINE_TYPE (MetaSelectionSourceMemory,
               meta_selection_source_memory,
               META_TYPE_SELECTION_SOURCE)

static void
meta_selection_source_memory_read_async (MetaSelectionSource *source,
                                         const char          *mimetype,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  MetaSelectionSourceMemory *source_mem = META_SELECTION_SOURCE_MEMORY (source);
  GInputStream *stream;
  g_autoptr (GTask) task = NULL;
  int mimetype_idx = -1;

  for (int i = 0; i < g_list_length (source_mem->mimetypes); i++)
    {
      char *other = g_list_nth_data (source_mem->mimetypes, i);
      if (g_strcmp0 (mimetype, other) == 0)
        {
          mimetype_idx = i;
          break;
        }
    }

  if (mimetype_idx == -1)
    {
      g_task_report_new_error (source, callback, user_data,
                               meta_selection_source_memory_read_async,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Mimetype not in selection");
      return;
    }

  task = g_task_new (source, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_selection_source_memory_read_async);

  GBytes *data = g_list_nth_data (source_mem->values, mimetype_idx);
  stream = g_memory_input_stream_new_from_bytes (data);
  g_task_return_pointer (task, stream, g_object_unref);
}

static GInputStream *
meta_selection_source_memory_read_finish (MetaSelectionSource  *source,
                                          GAsyncResult         *result,
                                          GError              **error)
{
  g_assert (g_task_get_source_tag (G_TASK (result)) ==
            meta_selection_source_memory_read_async);
  return g_task_propagate_pointer (G_TASK (result), error);
}

static GList *
meta_selection_source_memory_get_mimetypes (MetaSelectionSource *source)
{
  MetaSelectionSourceMemory *source_mem = META_SELECTION_SOURCE_MEMORY (source);

  return g_list_copy_deep (source_mem->mimetypes, (GCopyFunc) g_strdup, NULL);
}

static void
meta_selection_source_memory_finalize (GObject *object)
{
  MetaSelectionSourceMemory *source_mem = META_SELECTION_SOURCE_MEMORY (object);

  g_clear_list (&source_mem->mimetypes, g_free);
  for (int i = 0; i < g_list_length (source_mem->values); i++)
    {
      g_bytes_unref (g_list_nth_data (source_mem->values, i));
    }
  g_list_free(source_mem->values);

  G_OBJECT_CLASS (meta_selection_source_memory_parent_class)->finalize (object);
}

static void
meta_selection_source_memory_class_init (MetaSelectionSourceMemoryClass *klass)
{
  MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_selection_source_memory_finalize;

  source_class->read_async = meta_selection_source_memory_read_async;
  source_class->read_finish = meta_selection_source_memory_read_finish;
  source_class->get_mimetypes = meta_selection_source_memory_get_mimetypes;
}

static void
meta_selection_source_memory_init (MetaSelectionSourceMemory *source)
{
}

MetaSelectionSource *
meta_selection_source_memory_new (const char  *mimetype,
                                  GBytes      *content)
{
  MetaSelectionSourceMemory *source;

  g_return_val_if_fail (mimetype != NULL, NULL);
  g_return_val_if_fail (content != NULL, NULL);

  source = g_object_new (META_TYPE_SELECTION_SOURCE_MEMORY, NULL);
  source->mimetypes = g_list_append (NULL, g_strdup (mimetype));
  source->values = g_list_append (NULL, g_bytes_ref (content));

  return META_SELECTION_SOURCE (source);
}

/**
 * meta_selection_source_memory_new_multiple:
 * @mimetypes: (transfer none) (element-type utf8) (array zero-terminated=1): the array of mimetypes in the selection
 * @values: (transfer none) (element-type GBytes) (array zero-terminated=1): an array of `GBytes` matching the provided mimetypes
 *
 * Create a new [class@Meta.SelectionSource] with multiple mimetypes.
 *
 * Returns: (transfer full): a `MetaSelectionSource`
 */
MetaSelectionSource *
meta_selection_source_memory_new_multiple (char **mimetypes,
                                           GBytes **values)
{
  MetaSelectionSourceMemory *source;
  GList *mimetypes_list = NULL;
  GList *values_list = NULL;

  g_return_val_if_fail (mimetypes != NULL, NULL);
  g_return_val_if_fail (values != NULL, NULL);

  int i = 0;
  while (mimetypes[i] != NULL)
    {
      mimetypes_list = g_list_append(mimetypes_list, g_strdup (mimetypes[i]));
      values_list = g_list_append(values_list,  g_bytes_ref (values[i]));
      i++;
    }

  source = g_object_new (META_TYPE_SELECTION_SOURCE_MEMORY, NULL);

  source->mimetypes = mimetypes_list;
  source->values = values_list;

  return META_SELECTION_SOURCE (source);
}
