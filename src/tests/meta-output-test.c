/*
 * Copyright (C) 2016-2024 Red Hat, Inc.
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

#include "tests/meta-output-test.h"

G_DEFINE_TYPE (MetaOutputTest, meta_output_test, META_TYPE_OUTPUT_NATIVE)

G_DEFINE_TYPE (MetaBacklightTest, meta_backlight_test, META_TYPE_BACKLIGHT)

static GBytes *
meta_output_test_read_edid (MetaOutputNative *output_native)
{
  return NULL;
}

static void
meta_output_test_class_init (MetaOutputTestClass *klass)
{
  MetaOutputNativeClass *output_native_class = META_OUTPUT_NATIVE_CLASS (klass);

  output_native_class->read_edid = meta_output_test_read_edid;
}

static void
meta_output_test_init (MetaOutputTest *output_test)
{
  output_test->scale = 1;
}

static void
meta_backlight_test_set_brightness (MetaBacklight       *backlight,
                                    int                  brightness_target,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  MetaBacklightTest *backlight_test = META_BACKLIGHT_TEST (backlight);
  g_autoptr (GTask) task = NULL;

  task = g_task_new (backlight_test, cancellable, callback, user_data);
  g_task_set_task_data (task, GINT_TO_POINTER (brightness_target), NULL);

  g_task_return_int (task, brightness_target);
}

static int
meta_backlight_test_set_brightness_finish (MetaBacklight  *backlight,
                                           GAsyncResult   *result,
                                           GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, backlight), -1);

  return g_task_propagate_int (G_TASK (result), error);
}

static void
meta_backlight_test_class_init (MetaBacklightTestClass *klass)
{
  MetaBacklightClass *backlight_class = META_BACKLIGHT_CLASS (klass);

  backlight_class->set_brightness = meta_backlight_test_set_brightness;
  backlight_class->set_brightness_finish = meta_backlight_test_set_brightness_finish;
}

static void
meta_backlight_test_init (MetaBacklightTest *backlight_test)
{
}
