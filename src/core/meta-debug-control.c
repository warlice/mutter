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

#include "core/meta-debug-control.h"

#include "core/util-private.h"
#include "meta/meta-backend.h"
#include "meta/meta-context.h"

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

#define META_DEBUG_CONTROL_DBUS_SERVICE "org.gnome.Mutter.DebugControl"
#define META_DEBUG_CONTROL_DBUS_PATH "/org/gnome/Mutter/DebugControl"

struct _MetaDebugControl
{
  MetaDBusDebugControlSkeleton parent;

  MetaContext *context;

  guint dbus_name_id;
};

static void meta_dbus_debug_control_iface_init (MetaDBusDebugControlIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaDebugControl,
                         meta_debug_control,
                         META_DBUS_TYPE_DEBUG_CONTROL_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_DEBUG_CONTROL,
                                                meta_dbus_debug_control_iface_init))

static void
meta_dbus_debug_control_iface_init (MetaDBusDebugControlIface *iface)
{
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (user_data);
  g_autoptr (GError) error = NULL;

  meta_topic (META_DEBUG_BACKEND,
              "Acquired D-Bus name '%s', exporting service on '%s'",
              META_DEBUG_CONTROL_DBUS_SERVICE, META_DEBUG_CONTROL_DBUS_PATH);

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (debug_control),
                                         connection,
                                         META_DEBUG_CONTROL_DBUS_PATH,
                                         &error))
    {
      g_warning ("Failed to export '%s' object on '%s': %s",
                 META_DEBUG_CONTROL_DBUS_SERVICE,
                 META_DEBUG_CONTROL_DBUS_PATH,
                 error->message);
    }
}

static void
meta_debug_control_dispose (GObject *object)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  g_clear_handle_id (&debug_control->dbus_name_id, g_bus_unown_name);

  G_OBJECT_CLASS (meta_debug_control_parent_class)->dispose (object);
}

static void
meta_debug_control_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      debug_control->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_debug_control_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, debug_control->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_debug_control_class_init (MetaDebugControlClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_debug_control_dispose;
  object_class->set_property = meta_debug_control_set_property;
  object_class->get_property = meta_debug_control_get_property;

  obj_props[PROP_CONTEXT] = g_param_spec_object ("context", NULL, NULL,
                                                 META_TYPE_CONTEXT,
                                                 G_PARAM_CONSTRUCT_ONLY |
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_debug_control_init (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);
  gboolean enable_hdr, force_linear_blending;

  enable_hdr = g_strcmp0 (getenv ("MUTTER_DEBUG_ENABLE_HDR"), "1") == 0;
  meta_dbus_debug_control_set_enable_hdr (dbus_debug_control, enable_hdr);

  force_linear_blending =
    g_strcmp0 (getenv ("MUTTER_DEBUG_FORCE_LINEAR_BLENDING"), "1") == 0;
  meta_dbus_debug_control_set_force_linear_blending (dbus_debug_control,
                                                     force_linear_blending);
}

gboolean
meta_debug_control_is_linear_blending_forced (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);

  return meta_dbus_debug_control_get_force_linear_blending (dbus_debug_control);
}

gboolean
meta_debug_control_is_hdr_enabled (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);

  return meta_dbus_debug_control_get_enable_hdr (dbus_debug_control);
}

void
meta_debug_control_export (MetaDebugControl *debug_control)
{
  debug_control->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_DEBUG_CONTROL_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    NULL,
                    NULL,
                    debug_control,
                    NULL);
}
