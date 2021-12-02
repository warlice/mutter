/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include "backends/meta-remote-desktop.h"
#include "backends/meta-screen-cast.h"
#include "core/meta-sdk.h"

static const char * const viewer_path =
  MUTTER_LIBEXECDIR "/mutter-viewer";

struct _MetaSdk
{
  GObject parent;

  MetaContext *context;
  char *external_wayland_display;
  char *external_x11_display;

  GSubprocess *viewer_process;
  GCancellable *viewer_process_cancellable;
};

G_DEFINE_TYPE (MetaSdk, meta_sdk, G_TYPE_OBJECT)

static void
meta_sdk_finalize (GObject *object)
{
  MetaSdk *sdk = META_SDK (object);

  g_clear_pointer (&sdk->external_wayland_display, g_free);
  g_clear_pointer (&sdk->external_x11_display, g_free);
  if (sdk->viewer_process_cancellable)
    {
      g_cancellable_cancel (sdk->viewer_process_cancellable);
      g_clear_object (&sdk->viewer_process_cancellable);
    }
  g_clear_object (&sdk->viewer_process);

  G_OBJECT_CLASS (meta_sdk_parent_class)->finalize (object);
}

static void
meta_sdk_class_init (MetaSdkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_sdk_finalize;
}

static void
meta_sdk_init (MetaSdk *sdk)
{
}

static void
on_viewer_died (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  MetaSdk *sdk = META_SDK (user_data);
  MetaContext *context = meta_sdk_get_context (sdk);
  g_autoptr (GError) error = NULL;

  if (!g_subprocess_wait_finish (sdk->viewer_process, res, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;
    }

  meta_context_terminate (context);
}

static void
maybe_launch_viewer (gpointer  dependency,
                     MetaSdk  *sdk)
{
  MetaContext *context = meta_sdk_get_context (sdk);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaRemoteDesktop *remote_desktop = meta_backend_get_remote_desktop (backend);
  MetaScreenCast *screen_cast = meta_backend_get_screen_cast (backend);
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) subprocess = NULL;
  g_autoptr (GError) error = NULL;

  if (!meta_remote_desktop_is_enabled (remote_desktop) ||
      !meta_screen_cast_is_enabled (screen_cast))
    return;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);

  if (sdk->external_wayland_display)
    {
      g_subprocess_launcher_setenv (launcher,
                                    "WAYLAND_DISPLAY",
                                    sdk->external_wayland_display,
                                    TRUE);
    }
  else
    {
      g_subprocess_launcher_unsetenv (launcher, "WAYLAND_DISPLAY");
    }

  if (sdk->external_x11_display)
    {
      g_subprocess_launcher_setenv (launcher,
                                    "DISPLAY",
                                    sdk->external_x11_display,
                                    TRUE);
    }
  else
    {
      g_subprocess_launcher_unsetenv (launcher, "DISPLAY");
    }

  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            viewer_path,
                                            "--sdk",
                                            NULL);
  if (!subprocess)
    {
      g_warning ("Failed to launch viewer: %s", error->message);
      return;
    }

  sdk->viewer_process = g_steal_pointer (&subprocess);

  sdk->viewer_process_cancellable = g_cancellable_new ();
  g_subprocess_wait_async (sdk->viewer_process,
                           sdk->viewer_process_cancellable,
                           on_viewer_died,
                           sdk);
}

MetaSdk *
meta_sdk_new (MetaContext  *context,
              GError      **error)
{
  g_autoptr (MetaSdk) sdk = NULL;
  MetaBackend *backend = meta_context_get_backend (context);
  MetaRemoteDesktop *remote_desktop = meta_backend_get_remote_desktop (backend);
  MetaScreenCast *screen_cast = meta_backend_get_screen_cast (backend);

  sdk = g_object_new (META_TYPE_SDK, NULL);
  sdk->context = context;
  sdk->external_wayland_display = g_strdup (getenv ("WAYLAND_DISPLAY"));
  sdk->external_x11_display = g_strdup (getenv ("DISPLAY"));

  g_signal_connect_object (remote_desktop, "enabled",
                           G_CALLBACK (maybe_launch_viewer),
                           sdk, 0);
  g_signal_connect_object (screen_cast, "enabled",
                           G_CALLBACK (maybe_launch_viewer),
                           sdk, 0);

  return g_steal_pointer (&sdk);
}

MetaContext *
meta_sdk_get_context (MetaSdk *sdk)
{
  return sdk->context;
}
