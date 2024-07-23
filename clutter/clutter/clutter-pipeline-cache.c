/*
 * Copyright (C) 2023 Red Hat
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

#include "config.h"

#include "clutter-pipeline-cache.h"

typedef struct _PipelineGroupEntry
{
  GHashTable **slots;
  size_t n_slots;
} PipelineGroupEntry;

struct _ClutterPipelineCache
{
  GObject parent;

  GHashTable *groups;
};

G_DEFINE_TYPE (ClutterPipelineCache, clutter_pipeline_cache, G_TYPE_OBJECT)

static PipelineGroupEntry *
pipeline_group_entry_new (void)
{
  PipelineGroupEntry *group_entry;

  group_entry = g_new0 (PipelineGroupEntry, 1);

  return group_entry;
}

static void
pipeline_group_entry_free (PipelineGroupEntry *group_entry)
{
  size_t i;

  for (i = 0; i < group_entry->n_slots; i++)
    g_clear_pointer (&group_entry->slots[i], g_hash_table_unref);
  g_free (group_entry->slots);
  g_free (group_entry);
}

static void
clutter_pipeline_cache_dispose (GObject *object)
{
  ClutterPipelineCache *pipeline_cache = CLUTTER_PIPELINE_CACHE (object);

  g_clear_pointer (&pipeline_cache->groups, g_hash_table_unref);

  G_OBJECT_CLASS (clutter_pipeline_cache_parent_class)->dispose (object);
}

static void
clutter_pipeline_cache_class_init (ClutterPipelineCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = clutter_pipeline_cache_dispose;
}

static void
clutter_pipeline_cache_init (ClutterPipelineCache *pipeline_cache)
{
  pipeline_cache->groups =
    g_hash_table_new_full (NULL,
                           NULL,
                           NULL,
                           (GDestroyNotify) pipeline_group_entry_free);
}

static uint32_t
calculate_color_state_key (ClutterColorState *color_state)
{
  ClutterColorspace colorspace =
    clutter_color_state_get_colorspace (color_state);
  ClutterTransferFunction transfer_function =
    clutter_color_state_get_transfer_function (color_state);

  return (colorspace |
          transfer_function << 8);
}

static uint64_t
calculate_key (ClutterColorState *source_color_state,
               ClutterColorState *target_color_state)
{
  uint64_t source_key;
  uint64_t target_key;

  source_key = calculate_color_state_key (source_color_state);
  target_key = calculate_color_state_key (target_color_state);

  return (source_key |
          target_key << 32);
}

/**
 * clutter_pipeline_cache_get_pipeline: (skip)
 */
CoglPipeline *
clutter_pipeline_cache_get_pipeline (ClutterPipelineCache *pipeline_cache,
                                     ClutterPipelineGroup  group,
                                     int                   slot,
                                     ClutterColorState    *source_color_state,
                                     ClutterColorState    *target_color_state)
{
  PipelineGroupEntry *group_entry;
  uint64_t key;
  CoglPipeline *pipeline;

  group_entry = g_hash_table_lookup (pipeline_cache->groups, group);
  if (!group_entry)
    return NULL;

  if (slot >= group_entry->n_slots)
    return NULL;

  if (!group_entry->slots[slot])
    return NULL;

  key = calculate_key (source_color_state, target_color_state);
  pipeline = g_hash_table_lookup (group_entry->slots[slot], &key);

  if (pipeline)
    return cogl_pipeline_copy (pipeline);
  else
    return NULL;
}

/**
 * clutter_pipeline_cache_sdd_pipeline: (skip)
 */
void
clutter_pipeline_cache_set_pipeline (ClutterPipelineCache *pipeline_cache,
                                     ClutterPipelineGroup  group,
                                     int                   slot,
                                     ClutterColorState    *source_color_state,
                                     ClutterColorState    *target_color_state,
                                     CoglPipeline         *pipeline)
{
  PipelineGroupEntry *group_entry;
  uint64_t key;

  group_entry = g_hash_table_lookup (pipeline_cache->groups, group);
  if (!group_entry)
    {
      group_entry = pipeline_group_entry_new ();
      g_hash_table_insert (pipeline_cache->groups, group, group_entry);
    }

  if (slot >= group_entry->n_slots)
    {
      size_t new_n_slots;

      new_n_slots = slot + 1;
      group_entry->slots = g_realloc_n (group_entry->slots,
                                        new_n_slots,
                                        sizeof (GHashTable *));
      memset (group_entry->slots + group_entry->n_slots,
              0,
              (new_n_slots - group_entry->n_slots) * sizeof (GHashTable *));
      group_entry->n_slots = new_n_slots;
    }

  if (!group_entry->slots[slot])
    {
      group_entry->slots[slot] = g_hash_table_new_full (g_int64_hash,
                                                        g_int64_equal,
                                                        g_free,
                                                        g_object_unref);
    }

  key = calculate_key (source_color_state, target_color_state);
  g_hash_table_replace (group_entry->slots[slot],
                        g_memdup2 (&key, sizeof (key)),
                        g_object_ref (pipeline));
}

/**
 * clutter_pipeline_cache_unset_pipeline: (skip)
 */
void
clutter_pipeline_cache_unset_pipeline (ClutterPipelineCache *pipeline_cache,
                                       ClutterPipelineGroup  group,
                                       int                   slot,
                                       ClutterColorState    *source_color_state,
                                       ClutterColorState    *target_color_state)

{
  PipelineGroupEntry *group_entry;
  uint64_t key;

  group_entry = g_hash_table_lookup (pipeline_cache->groups, group);

  if (!group_entry)
    return;

  if (slot >= group_entry->n_slots)
    return;

  if (!group_entry->slots[slot])
    return;

  key = calculate_key (source_color_state, target_color_state);
  g_hash_table_remove (group_entry->slots[slot], &key);
}

/**
 * clutter_pipeline_cache_unset_all_pipeline: (skip)
 */
void
clutter_pipeline_cache_unset_all_pipelines (ClutterPipelineCache *pipeline_cache,
                                            ClutterPipelineGroup  group)
{
  g_hash_table_remove (pipeline_cache->groups, group);
}
