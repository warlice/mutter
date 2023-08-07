/*
 * Copyright (C) 2023 Red Hat Inc.
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

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wayland/meta-wayland-security-context.h"

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-filter-manager.h"

#include "security-context-v1-server-protocol.h"

struct _MetaWaylandAuthClient
{
  GObject parent_instance;
  MetaWaylandSecurityContextManager *manager;

  struct wl_client *wayland_client;
  struct wl_listener client_destroy_listener;

  MetaWaylandSecurityContextAuthentication auth;
};

G_DEFINE_TYPE (MetaWaylandAuthClient, meta_wayland_auth_client, G_TYPE_OBJECT)

typedef struct _MetaWaylandSecurityContext
{
  MetaWaylandSecurityContextManager *manager;
  struct wl_list link;

  MetaWaylandSecurityContextAuthentication auth;
  int listen_fd;
  int close_fd;

  struct wl_event_source *listen_source;
  struct wl_event_source *close_source;
} MetaWaylandSecurityContext;

typedef struct _MetaWaylandSecurityContextManager
{
  MetaWaylandCompositor *compositor;
  GHashTable *clients;
  struct wl_list security_context_list;
} MetaWaylandSecurityContextManager;

static void
meta_wayland_security_context_free (MetaWaylandSecurityContext *security_context);

static void
meta_wayland_security_context_authentication_finalize (MetaWaylandSecurityContextAuthentication *auth);

const MetaWaylandSecurityContextAuthentication *
meta_wayland_auth_client_get_authentication (MetaWaylandAuthClient *client)
{
  return &client->auth;
}

static void
meta_wayland_auth_client_finalize (GObject *object)
{
  MetaWaylandAuthClient *client = META_WAYLAND_AUTH_CLIENT (object);

  wl_list_remove (&client->client_destroy_listener.link);
  meta_wayland_security_context_authentication_finalize (&client->auth);

  G_OBJECT_CLASS (meta_wayland_auth_client_parent_class)->finalize (object);
}

static void
meta_wayland_auth_client_class_init (MetaWaylandAuthClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_auth_client_finalize;
}

static void
meta_wayland_auth_client_init (MetaWaylandAuthClient *client)
{
  wl_list_init (&client->client_destroy_listener.link);
}

static void
meta_wayland_security_context_authentication_finalize (MetaWaylandSecurityContextAuthentication *auth)
{
  g_clear_pointer (&auth->sandbox_engine, g_free);
  g_clear_pointer (&auth->app_id, g_free);
  g_clear_pointer (&auth->instance_id, g_free);
}

static void
meta_wayland_security_context_authentication_copy (MetaWaylandSecurityContextAuthentication *src,
                                                   MetaWaylandSecurityContextAuthentication *dst)
{
  dst->sandbox_engine = g_strdup (src->sandbox_engine);
  dst->app_id = g_strdup (src->app_id);
  dst->instance_id = g_strdup (src->instance_id);
}

static void
meta_wayland_auth_client_handle_destroy (struct wl_listener *listener,
                                         void               *data)
{
  MetaWaylandAuthClient *auth_client =
    wl_container_of (listener, auth_client, client_destroy_listener);
  MetaWaylandSecurityContextManager *manager = auth_client->manager;

  meta_topic (META_DEBUG_WAYLAND,
              "Client disconnected with security context "
              "(engine: %s, app id: %s, instance id: %s)",
              auth_client->auth.sandbox_engine,
              auth_client->auth.app_id,
              auth_client->auth.instance_id);

  g_hash_table_remove (manager->clients, auth_client->wayland_client);
}

static int
security_context_listen_fd_event (int       listen_fd,
                                  uint32_t  mask,
                                  void     *data)
{
  MetaWaylandSecurityContext *security_context = data;
  MetaWaylandSecurityContextManager *manager = security_context->manager;
  MetaWaylandAuthClient *auth_client;
  struct wl_client *wayland_client;
  int client_fd;

  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))
    {
      meta_wayland_security_context_free (security_context);
      return 0;
    }

  if (!(mask & WL_EVENT_READABLE))
    return 0;

  client_fd = accept (listen_fd, NULL, NULL);

  if (client_fd < 0)
    {
      g_warning ("Failed to accept client");
      return 0;
    }

  wayland_client = wl_client_create (manager->compositor->wayland_display,
                                     client_fd);

  auth_client = g_object_new (META_TYPE_WAYLAND_AUTH_CLIENT, NULL);
  auth_client->manager = manager;
  auth_client->wayland_client = wayland_client;
  auth_client->client_destroy_listener.notify =
    meta_wayland_auth_client_handle_destroy;
  wl_client_add_destroy_listener (wayland_client,
                                  &auth_client->client_destroy_listener);

  meta_wayland_security_context_authentication_copy (&security_context->auth,
                                                     &auth_client->auth);

  g_hash_table_insert (manager->clients, wayland_client, auth_client);

  meta_topic (META_DEBUG_WAYLAND,
              "New client with security context "
              "(engine: %s, app id: %s, instance id: %s)",
              auth_client->auth.sandbox_engine,
              auth_client->auth.app_id,
              auth_client->auth.instance_id);

  return 0;
}

static int
security_context_close_fd_event (int       fd,
                                 uint32_t  mask,
                                 void     *data)
{
  MetaWaylandSecurityContext *security_context = data;

  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))
    meta_wayland_security_context_free (security_context);

  return 0;
}

static void
security_context_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static gboolean
check_metadata (MetaWaylandSecurityContext *security_context,
                struct wl_resource         *resource)
{
  if (!security_context->auth.sandbox_engine)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_V1_ERROR_INVALID_METADATA,
                              "no sandbox engine specified");
      return FALSE;
    }

  if (g_strcmp0 (security_context->auth.sandbox_engine, "flatpak") == 0)
    {
      if (!security_context->auth.app_id ||
          !security_context->auth.instance_id)
        {
          wl_resource_post_error (resource,
                                  WP_SECURITY_CONTEXT_V1_ERROR_INVALID_METADATA,
                                  "flatpak requires app id and instance id");
          return FALSE;
        }
    }

  return TRUE;
}

static void
security_context_commit (struct wl_client   *client,
                         struct wl_resource *resource)
{
  MetaWaylandSecurityContext *security_context =
    wl_resource_get_user_data (resource);
  struct wl_event_loop *loop;

  if (!check_metadata (security_context, resource))
    return;

  if (!security_context)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED,
                              "already committed");
      return;
    }

  loop = wl_display_get_event_loop (wl_client_get_display (client));

  security_context->listen_source =
    wl_event_loop_add_fd (loop,
                          security_context->listen_fd,
                          WL_EVENT_READABLE,
                          security_context_listen_fd_event,
                          security_context);

  if (!security_context->listen_source)
    {
      wl_resource_post_no_memory (resource);
      return;
    }

  security_context->close_source =
    wl_event_loop_add_fd (loop,
                          security_context->close_fd,
                          0,
                          security_context_close_fd_event,
                          security_context);

  if (!security_context->close_source)
    {
      wl_resource_post_no_memory (resource);
      return;
    }

  meta_topic (META_DEBUG_WAYLAND,
              "Started listening on a socket for security context "
              "(engine: %s, app id: %s, instance id: %s)",
              security_context->auth.sandbox_engine,
              security_context->auth.app_id,
              security_context->auth.instance_id);

  wl_resource_set_user_data (resource, NULL);
}

static void
security_context_set_sandbox_engine (struct wl_client   *client,
                                     struct wl_resource *resource,
                                     const char         *sandbox_engine)
{
  MetaWaylandSecurityContext *security_context =
    wl_resource_get_user_data (resource);

  if (!security_context)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED,
                              "already committed");
      return;
    }

  if (security_context->auth.sandbox_engine)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_SET,
                              "sandbox engine already set");
      return;
    }

  security_context->auth.sandbox_engine = g_strdup (sandbox_engine);
}

static void
security_context_set_app_id (struct wl_client   *client,
                             struct wl_resource *resource,
                             const char         *app_id)
{
  MetaWaylandSecurityContext *security_context =
    wl_resource_get_user_data (resource);

  if (!security_context)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED,
                              "already committed");
      return;
    }

  if (security_context->auth.app_id)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_SET,
                              "app id already set");
      return;
    }

  security_context->auth.app_id = g_strdup (app_id);
}

static void
security_context_set_instance_id (struct wl_client   *client,
                                  struct wl_resource *resource,
                                  const char         *instance_id)
{
  MetaWaylandSecurityContext *security_context =
    wl_resource_get_user_data (resource);

  if (!security_context)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED,
                              "already committed");
      return;
    }

  if (security_context->auth.instance_id)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_SET,
                              "instance already set");
      return;
    }

  security_context->auth.instance_id = g_strdup (instance_id);
}

static const struct wp_security_context_v1_interface security_context_implementation = {
  .destroy = security_context_destroy,
  .commit = security_context_commit,
  .set_sandbox_engine = security_context_set_sandbox_engine,
  .set_app_id = security_context_set_app_id,
  .set_instance_id = security_context_set_instance_id,
};

static MetaWaylandSecurityContext *
meta_wayland_security_context_new (MetaWaylandSecurityContextManager *manager,
                                   int                                listen_fd,
                                   int                                close_fd)
{
  MetaWaylandSecurityContext *security_context;

  security_context = g_new0 (MetaWaylandSecurityContext, 1);
  security_context->manager = manager;
	wl_list_insert (&manager->security_context_list, &security_context->link);

  security_context->listen_fd = listen_fd;
  security_context->close_fd = close_fd;

  return security_context;
}

static void
meta_wayland_security_context_free (MetaWaylandSecurityContext *security_context)
{
  meta_topic (META_DEBUG_WAYLAND,
              "Destroying security context "
              "(engine: %s, app id: %s, instance id: %s)",
              security_context->auth.sandbox_engine,
              security_context->auth.app_id,
              security_context->auth.instance_id);

  wl_list_remove (&security_context->link);
  meta_wayland_security_context_authentication_finalize (&security_context->auth);

  if (security_context->listen_source)
    wl_event_source_remove (security_context->listen_source);
  if (security_context->close_source)
    wl_event_source_remove (security_context->close_source);

  close (security_context->listen_fd);
  close (security_context->close_fd);

  free (security_context);
}

static void
security_context_resource_destroy (struct wl_resource *resource)
{
  MetaWaylandSecurityContext *security_context =
    wl_resource_get_user_data (resource);

  /* We set the resource user data to NULL on commit. In this case the
   * close_fd source is responsible for freeing the security context.  */
  if (security_context)
    meta_wayland_security_context_free (security_context);
}

static void
manager_destroy (struct wl_client   *client,
                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static gboolean
check_listen_fd (int                 listen_fd,
                 struct wl_resource *resource)
{
  struct stat stat_buf;
  int accept_conn;
  socklen_t accept_conn_size = sizeof (accept_conn);

  if (fstat(listen_fd, &stat_buf) != 0)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_INVALID_LISTEN_FD,
                              "fstat on listen_fd failed");
      return FALSE;
    }

  if (!S_ISSOCK (stat_buf.st_mode))
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_INVALID_LISTEN_FD,
                              "listen_fd is not a socket");
      return FALSE;
    }

  if (getsockopt (listen_fd, SOL_SOCKET, SO_ACCEPTCONN,
                  &accept_conn, &accept_conn_size) != 0)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_INVALID_LISTEN_FD,
                              "getsockopt on listen_fd failed");
      return FALSE;
    }

  if (accept_conn == 0)
    {
      wl_resource_post_error (resource,
                              WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_INVALID_LISTEN_FD,
                              "listen_fd is not a listening socket");
      return FALSE;
    }

  return TRUE;
}

static void
manager_handle_create_listener(struct wl_client   *client,
                               struct wl_resource *manager_resource,
                               uint32_t            id,
                               int                 listen_fd,
                               int                 close_fd)
{
  MetaWaylandSecurityContextManager *manager =
    wl_resource_get_user_data (manager_resource);
  MetaWaylandSecurityContext *security_context;
  struct wl_resource *resource;

  if (!check_listen_fd (listen_fd, manager_resource))
    return;

  resource = wl_resource_create (client,
                                 &wp_security_context_v1_interface,
                                 wl_resource_get_version(manager_resource),
                                 id);

  if (!resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  security_context = meta_wayland_security_context_new (manager,
                                                        listen_fd,
                                                        close_fd);

  wl_resource_set_implementation (resource,
                                  &security_context_implementation,
                                  security_context,
                                  security_context_resource_destroy);
}


static const struct wp_security_context_manager_v1_interface manager_implementation = {
  .destroy = manager_destroy,
  .create_listener = manager_handle_create_listener,
};

static void
meta_wayland_security_context_manager_bind (struct wl_client *client,
                                            void             *user_data,
                                            uint32_t          version,
                                            uint32_t          id)
{
  MetaWaylandSecurityContextManager *manager = user_data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_security_context_manager_v1_interface,
                                 version, id);
  if (!resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &manager_implementation,
                                  manager, NULL);
}

MetaWaylandAuthClient *
meta_wayland_security_context_manager_get_auth_client (MetaWaylandSecurityContextManager *manager,
                                                       const struct wl_client            *client)
{
  return g_hash_table_lookup (manager->clients, client);
}

static MetaWaylandAccess
auth_client_filter (const struct wl_client *client,
                    const struct wl_global *global,
                    gpointer                user_data)
{
  MetaWaylandSecurityContextManager *manager = user_data;
  MetaWaylandAuthClient *auth_client;

  auth_client =
    meta_wayland_security_context_manager_get_auth_client (manager, client);

  return auth_client ? META_WAYLAND_ACCESS_DENIED : META_WAYLAND_ACCESS_ALLOWED;
}

static MetaWaylandSecurityContextManager *
meta_wayland_security_context_manager_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandSecurityContextManager *manager;
  MetaWaylandFilterManager *filter_manager =
    meta_wayland_compositor_get_filter_manager (compositor);
  struct wl_global *global;

  manager = g_new0 (MetaWaylandSecurityContextManager, 1);
  manager->compositor = compositor;
  manager->clients =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) g_object_unref);
  wl_list_init (&manager->security_context_list);

  global = wl_global_create (compositor->wayland_display,
                             &wp_security_context_manager_v1_interface,
                             META_WP_SECURITY_CONTEXT_V1_VERSION,
                             manager,
                             meta_wayland_security_context_manager_bind);

  if (global)
    {
      meta_wayland_filter_manager_add_global (filter_manager,
                                              global,
                                              auth_client_filter,
                                              manager);
    }
  else
    {
      g_warning ("Failed to create wp_security_context_manager_v1 global");
    }

  return manager;
}

static void
meta_wayland_security_context_manager_free (MetaWaylandSecurityContextManager *manager)
{
  MetaWaylandSecurityContext *security_context, *tmp;

  g_clear_pointer (&manager->clients, g_hash_table_unref);

  wl_list_for_each_safe (security_context, tmp, &manager->security_context_list, link)
    meta_wayland_security_context_free (security_context);
}

void
meta_wayland_security_context_init (MetaWaylandCompositor *compositor)
{
  compositor->security_context_manager = meta_wayland_security_context_manager_new (compositor);
}

void
meta_wayland_security_context_finalize (MetaWaylandCompositor *compositor)
{
  g_clear_pointer (&compositor->security_context_manager,
                   meta_wayland_security_context_manager_free);
}
