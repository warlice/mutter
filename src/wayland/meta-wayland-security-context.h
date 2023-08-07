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

#ifndef META_WAYLAND_SECURITY_CONTEXT_H
#define META_WAYLAND_SECURITY_CONTEXT_H

#include <glib-object.h>
#include <wayland-server.h>

#include "wayland/meta-wayland-types.h"

G_BEGIN_DECLS

typedef struct _MetaWaylandSecurityContextAuthentication
{
  char *sandbox_engine;
  char *app_id;
  char *instance_id;
} MetaWaylandSecurityContextAuthentication;

#define META_TYPE_WAYLAND_AUTH_CLIENT (meta_wayland_auth_client_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandAuthClient, meta_wayland_auth_client, META, WAYLAND_AUTH_CLIENT, GObject)

void meta_wayland_security_context_init (MetaWaylandCompositor *compositor);

void meta_wayland_security_context_finalize (MetaWaylandCompositor *compositor);

MetaWaylandAuthClient *
meta_wayland_security_context_manager_get_auth_client (MetaWaylandSecurityContextManager *manager,
                                                       const struct wl_client            *client);

const MetaWaylandSecurityContextAuthentication *
meta_wayland_auth_client_get_authentication (MetaWaylandAuthClient *client);

G_END_DECLS

#endif /* META_WAYLAND_SECURITY_CONTEXT_H */
