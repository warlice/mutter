/*
 * Copyright (C) 2021 Red Hat Inc.
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
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "mdk-context.h"
#include "mdk-monitor.h"
#include "mdk-session.h"

static void
activate_about (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  GtkApplication *app = user_data;
  const char *authors[] = {
    _("The Mutter Team"),
    NULL
  };

  gtk_show_about_dialog (GTK_WINDOW (gtk_application_get_active_window (app)),
                         "program-name", _("Mutter SDK"),
                         "version", VERSION,
                         "copyright", "© 2001—2021 The Mutter Team",
                         "license-type", GTK_LICENSE_GPL_2_0,
                         "website", "http://gitlab.gnome.org/GNOME/mutter",
                         "comments", _("Mutter and GNOME Shell software development kit"),
                         "authors", authors,
                         "logo-icon-name", "org.gnome.Mutter.Sdk",
                         "title", _("About Mutter SDK"),
                         NULL);
}

static void
on_context_ready (MdkContext   *context,
                  GApplication *app)
{
  GList *windows;
  GtkWindow *window;
  MdkMonitor *monitor;

  windows = gtk_application_get_windows (GTK_APPLICATION (app));
  g_warn_if_fail (g_list_length (windows) == 1);

  window = windows->data;

  monitor = mdk_monitor_new (context);
  gtk_window_set_child (window, GTK_WIDGET (monitor));
  gtk_window_set_focus (window, GTK_WIDGET (monitor));
}

static void
on_context_error (MdkContext   *context,
                  GError       *error,
                  GApplication *app)
{
  g_warning ("Context got an error: %s", error->message);
}

static void
activate (GApplication *app,
          MdkContext   *context)
{
  g_autoptr (GtkBuilder) builder = NULL;
  GtkWidget *window;

  builder = gtk_builder_new_from_resource ("/ui/mdk-viewer.ui");

  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));
  gtk_widget_show (window);

  g_signal_connect (context, "ready", G_CALLBACK (on_context_ready), app);
  g_signal_connect (context, "error", G_CALLBACK (on_context_error), app);
  mdk_context_activate (context);
}

static void
print_version (void)
{
  g_print ("mutter-viewer %s\n", VERSION);
}

static int
local_options (GApplication *app,
               GVariantDict *options,
               gpointer      data)
{
  gboolean version = FALSE;

  g_variant_dict_lookup (options, "version", "b", &version);

  if (version)
    {
      print_version ();
      return 0;
    }

  return -1;
}

static gboolean
transform_action_state_to (GBinding     *binding,
                           const GValue *from_value,
                           GValue       *to_value,
                           gpointer      user_data)
{
  GVariant *state_variant;

  state_variant = g_value_get_variant (from_value);
  if (g_variant_is_of_type (state_variant, G_VARIANT_TYPE_BOOLEAN))
    {
      g_value_set_boolean (to_value, g_variant_get_boolean (state_variant));
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static void
bind_action_to_property (GtkApplication *app,
                         const char     *action_name,
                         gpointer        object,
                         const char     *property)
{
  GAction *action;
  GParamSpec *pspec;

  action = g_action_map_lookup_action (G_ACTION_MAP (app), action_name);
  g_return_if_fail (action);

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), property);
  
  g_object_bind_property_full (action, "state", object, property,
                               G_BINDING_SYNC_CREATE,
                               transform_action_state_to,
                               NULL, 
                               g_param_spec_ref (pspec),
                               (GDestroyNotify) g_param_spec_unref);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MdkContext) context = NULL;
  g_autoptr (GtkApplication) app = NULL;
  static GActionEntry app_entries[] = {
    { "about", activate_about, NULL, NULL, NULL },
    { "toggle_emulate_touch", .state = "false", },
  };
  struct {
    const char *action_and_target;
    const char *accelerators[2];
  } accels[] = {
    { "app.about", { "F1", NULL } },
  };
  gboolean is_sdk;
  GOptionEntry options[] = {
    {
      "sdk", 0, 0, G_OPTION_ARG_NONE,
      &is_sdk,
      N_("Used by mutter's SDK mode"),
      NULL
    },
    { NULL }
  };
  int i;

  g_set_prgname ("org.gnome.Mutter.Sdk");

  context = mdk_context_new ();

  app = gtk_application_new ("org.gnome.Mutter.Sdk",
                             G_APPLICATION_NON_UNIQUE);

  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   app_entries, G_N_ELEMENTS (app_entries),
                                   app);
  for (i = 0; i < G_N_ELEMENTS (accels); i++)
    {
      gtk_application_set_accels_for_action (app,
                                             accels[i].action_and_target,
                                             accels[i].accelerators);
    }

  bind_action_to_property (app, "toggle_emulate_touch",
                           context, "emulate-touch");

  g_application_add_main_option (G_APPLICATION (app),
                                 "version", 0, 0, G_OPTION_ARG_NONE,
                                 "Show version", NULL);
  g_application_add_main_option_entries (G_APPLICATION (app), options);

  g_signal_connect (app, "activate", G_CALLBACK (activate), context);
  g_signal_connect (app, "handle-local-options", G_CALLBACK (local_options), NULL);

  g_application_run (G_APPLICATION (app), argc, argv);

  return EXIT_SUCCESS;
}
