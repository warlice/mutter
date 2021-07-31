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

#include "mdk-monitor.h"

#include <gtk/gtk.h>

#include "mdk-context.h"
#include "mdk-session.h"
#include "mdk-stream.h"

#define DEFAULT_MONITOR_WIDTH 1280
#define DEFAULT_MONITOR_HEIGHT 800

struct _MdkMonitor
{
  GtkBox parent;

  GtkPicture *picture;

  MdkContext *context;
  MdkStream *stream;
};

G_DEFINE_TYPE (MdkMonitor, mdk_monitor, GTK_TYPE_BOX)

static void
mdk_monitor_realize (GtkWidget *widget)
{
  MdkMonitor *monitor = MDK_MONITOR (widget);
  GdkSurface *surface;

  GTK_WIDGET_CLASS (mdk_monitor_parent_class)->realize (widget);

  surface = gtk_native_get_surface (gtk_widget_get_native (widget));
  mdk_stream_realize (monitor->stream, surface);
}

static void
mdk_monitor_unrealize (GtkWidget *widget)
{
  MdkMonitor *monitor = MDK_MONITOR (widget);
  GdkSurface *surface;

  surface = gtk_native_get_surface (gtk_widget_get_native (widget));
  mdk_stream_unrealize (monitor->stream, surface);

  GTK_WIDGET_CLASS (mdk_monitor_parent_class)->unrealize (widget);
}

static void
mdk_monitor_map (GtkWidget *widget)
{
  MdkMonitor *monitor = MDK_MONITOR (widget);

  GTK_WIDGET_CLASS (mdk_monitor_parent_class)->map (widget);

  mdk_stream_map (monitor->stream);
}

static void
mdk_monitor_unmap (GtkWidget *widget)
{
  MdkMonitor *monitor = MDK_MONITOR (widget);

  mdk_stream_unmap (monitor->stream);

  GTK_WIDGET_CLASS (mdk_monitor_parent_class)->unmap (widget);
}

static gboolean
mdk_monitor_focus (GtkWidget        *widget,
                   GtkDirectionType  direction)
{
  if (!gtk_widget_is_focus (widget))
    {
      gtk_widget_grab_focus (widget);
      return TRUE;
    }

  return FALSE;
}

static void
mdk_monitor_finalize (GObject *object)
{
  MdkMonitor *monitor = MDK_MONITOR (object);

  g_clear_object (&monitor->stream);

  G_OBJECT_CLASS (mdk_monitor_parent_class)->finalize (object);
}

static void
mdk_monitor_class_init (MdkMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = mdk_monitor_finalize;

  widget_class->realize = mdk_monitor_realize;
  widget_class->unrealize = mdk_monitor_unrealize;
  widget_class->map = mdk_monitor_map;
  widget_class->unmap = mdk_monitor_unmap;
  widget_class->focus = mdk_monitor_focus;
}

static void
mdk_monitor_init (MdkMonitor *monitor)
{
}

MdkMonitor *
mdk_monitor_new (MdkContext *context)
{
  MdkSession *session = mdk_context_get_session (context);
  MdkMonitor *monitor;
  GdkPaintable *paintable;

  monitor = g_object_new (MDK_TYPE_MONITOR,
                          "orientation", GTK_ORIENTATION_VERTICAL,
                          "vexpand", TRUE,
                          "hexpand", TRUE,
                          NULL);
  monitor->context = context;
  monitor->stream = mdk_stream_new (session,
                                    DEFAULT_MONITOR_WIDTH,
                                    DEFAULT_MONITOR_HEIGHT);
  paintable = GDK_PAINTABLE (monitor->stream);
  monitor->picture = GTK_PICTURE (gtk_picture_new_for_paintable (paintable));
  gtk_widget_set_sensitive (GTK_WIDGET (monitor->picture), FALSE);
  gtk_box_append (GTK_BOX (monitor), GTK_WIDGET (monitor->picture));

  return monitor;
}
