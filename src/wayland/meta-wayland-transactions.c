/*
 * Copyright (C) 2020 Red Hat Inc.
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
 */

#include "config.h"

#include "wayland/meta-wayland-transactions.h"

#include <glib.h>
#include <wayland-server-core.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

#include "transactions-v1-server-protocol.h"

typedef struct _MetaWaylandTransaction
{
  MetaWaylandTransactions *transactions;
  GHashTable *cached_states;
} MetaWaylandTransaction;

struct _MetaWaylandTransactions
{
  GList *transactions;
};

static MetaWaylandSurfaceState *
ensure_cached_state (MetaWaylandTransaction *transaction,
                     MetaWaylandSurface     *surface)
{
  MetaWaylandSurfaceState *cached_state;

  cached_state = g_hash_table_lookup (transaction->cached_states, surface);
  if (cached_state)
    return cached_state;

  cached_state = g_object_new (META_TYPE_WAYLAND_SURFACE_STATE, NULL);
  g_hash_table_insert (transaction->cached_states, surface, cached_state);
  return cached_state;
}

static gboolean
meta_wayland_transaction_state_fence (MetaWaylandSurface      *surface,
                                      MetaWaylandSurfaceState *pending,
                                      gpointer                 user_data)
{
  MetaWaylandTransaction *transaction = user_data;
  MetaWaylandSurfaceState *cached_state;

  cached_state = ensure_cached_state (transaction, surface);
  meta_wayland_surface_state_discard_presentation_feedback (cached_state);
  meta_wayland_surface_state_merge_into (pending, cached_state);
  return TRUE;
}

static void
on_surface_destroy (MetaWaylandSurface     *surface,
                    MetaWaylandTransaction *transaction)
{
  g_hash_table_remove (transaction->cached_states, surface);
  meta_wayland_surface_remove_state_fence (surface,
                                           meta_wayland_transaction_state_fence);
}

static gboolean
surface_belongs_to_transaction (MetaWaylandSurface      *surface,
                                MetaWaylandTransactions *transactions)
{
  GList *l;

  for (l = transactions->transactions; l; l = l->next)
    {
      MetaWaylandTransaction *transaction = l->data;

      if (g_hash_table_contains (transaction->cached_states, surface))
        return TRUE;
    }

  return FALSE;
}

static void
wp_transaction_add_surface (struct wl_client   *client,
                            struct wl_resource *resource,
                            struct wl_resource *surface_resource)
{
  MetaWaylandTransaction *transaction = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandStateFencePriority fence_priority;

  if (surface_belongs_to_transaction (surface, transaction->transactions))
    {
      wl_resource_post_error (resource, WP_TRANSACTION_V1_ERROR_ALREADY_USED,
                              "wl_surface@%d already belongs to a transaction",
                              wl_resource_get_id (surface_resource));
      return;
    }

  g_hash_table_insert (transaction->cached_states, surface, NULL);
  g_signal_connect (surface, "destroy", G_CALLBACK (on_surface_destroy),
                    transaction);

  fence_priority = META_WAYLAND_STATE_FENCE_PRIORITY_TRANSACTION;
  meta_wayland_surface_add_state_fence (surface,
                                        fence_priority,
                                        meta_wayland_transaction_state_fence,
                                        transaction);
}

static void
wp_transaction_commit (struct wl_client   *client,
                       struct wl_resource *resource)
{
  MetaWaylandTransaction *transaction = wl_resource_get_user_data (resource);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, transaction->cached_states);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      MetaWaylandSurface *surface = key;
      MetaWaylandSurfaceState *cached_state = value;
      MetaWaylandStateFencePriority fence_priority;

      if (!cached_state)
        continue;

      fence_priority = META_WAYLAND_STATE_FENCE_PRIORITY_TRANSACTION;
      meta_wayland_surface_commit_past_fence (surface, cached_state,
                                              fence_priority);
    }

  wl_resource_destroy (resource);
}

static const struct wp_transaction_v1_interface meta_wp_transaction_interface = {
  wp_transaction_add_surface,
  wp_transaction_commit,
};

static void
wp_transaction_manager_destroy (struct wl_client   *client,
                                struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wp_transaction_destructor (struct wl_resource *resource)
{
  MetaWaylandTransaction *transaction = wl_resource_get_user_data (resource);
  MetaWaylandTransactions *transactions = transaction->transactions;
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, transaction->cached_states);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      MetaWaylandSurface *surface = key;

      g_signal_handlers_disconnect_by_data (surface, transaction);
      meta_wayland_surface_remove_state_fence (surface,
                                               meta_wayland_transaction_state_fence);
    }
  g_hash_table_unref (transaction->cached_states);

  transactions->transactions = g_list_remove (transactions->transactions,
                                              transaction);

  g_free (transaction);
}

static void
maybe_unref_object (gpointer data)
{
  if (data)
    g_object_unref (data);
}

static void
wp_transaction_manager_create_transaction (struct wl_client   *client,
                                           struct wl_resource *resource,
                                           uint32_t            id)
{
  MetaWaylandTransactions *transactions = wl_resource_get_user_data (resource);
  struct wl_resource *transaction_resource;
  MetaWaylandTransaction *transaction;

  transaction_resource = wl_resource_create (client,
                                             &wp_transaction_v1_interface,
                                             wl_resource_get_version (resource),
                                             id);
  if (!transaction_resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  transaction = g_new0 (MetaWaylandTransaction, 1);
  transaction->transactions = transactions;
  transaction->cached_states = g_hash_table_new_full (NULL,
                                                      NULL,
                                                      NULL,
                                                      maybe_unref_object);

  wl_resource_set_implementation (transaction_resource,
                                  &meta_wp_transaction_interface,
                                  transaction,
                                  wp_transaction_destructor);

  transactions->transactions = g_list_prepend (transactions->transactions,
                                               transaction);
}

static const struct wp_transaction_manager_v1_interface
meta_wp_transaction_manager_interface = {
  wp_transaction_manager_destroy,
  wp_transaction_manager_create_transaction,
};

static void
bind_transaction_manager (struct wl_client *client,
                          void             *data,
                          uint32_t          version,
                          uint32_t          id)
{
  MetaWaylandTransactions *transactions = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_transaction_manager_v1_interface,
                                 META_WP_TRANSACTION_MANAGER_V1_VERSION,
                                 id);
  if (!resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &meta_wp_transaction_manager_interface,
                                  transactions,
                                  NULL);
}

gboolean
meta_wayland_init_transactions (MetaWaylandCompositor *compositor)
{
  MetaWaylandTransactions *transactions;

  transactions = g_new0 (MetaWaylandTransactions, 1);

  if (!wl_global_create (compositor->wayland_display,
                         &wp_transaction_manager_v1_interface, 1,
                         transactions,
                         bind_transaction_manager))
    return FALSE;

  compositor->transactions = transactions;

  return TRUE;
}
