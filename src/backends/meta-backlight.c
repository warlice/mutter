/*
 * Copyright (C) 2024 Red Hat
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

#include "backends/meta-backlight-private.h"

#include <glib.h>

enum
{
  PROP_0,

  PROP_NAME,

  PROP_BRIGHTNESS_MIN,
  PROP_BRIGHTNESS_MAX,
  PROP_BRIGHTNESS,

  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _MetaBacklightPrivate
{
  char *name;

  int brightness_min;
  int brightness_max;
  int brightness_target;
  gboolean pending;
} MetaBacklightPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaBacklight, meta_backlight, G_TYPE_OBJECT)

const char *
meta_backlight_get_name (MetaBacklight *backlight)
{
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);

  return priv->name;
}

gboolean
meta_backlight_has_pending (MetaBacklight *backlight)
{
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);

  return priv->pending;
}

void
meta_backlight_update_brightness_target (MetaBacklight *backlight,
                                         int            brightness)
{
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);
  int new_brightness;

  if (priv->brightness_target == brightness)
    return;

  new_brightness = CLAMP (brightness,
                          priv->brightness_min, priv->brightness_max);

  if (brightness != new_brightness)
    {
      g_warning ("Trying to set out-of-range brightness on %s (%s)",
                 priv->name,
                 g_type_name (G_OBJECT_TYPE (backlight)));
    }

  priv->brightness_target = new_brightness;
  g_object_notify_by_pspec (G_OBJECT (backlight), obj_props[PROP_BRIGHTNESS]);
}

static void
on_brightness_set (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  MetaBacklight *backlight = META_BACKLIGHT (source_object);
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);
  g_autoptr (GError) error = NULL;
  int brightness;

  priv->pending = FALSE;

  brightness =
    META_BACKLIGHT_GET_CLASS (backlight)->set_brightness_finish (backlight,
                                                                 res,
                                                                 &error);
  if (brightness < 0)
    {
      g_warning ("Setting backlight on %s (%s) failed: %s",
                 priv->name,
                 g_type_name (G_OBJECT_TYPE (backlight)),
                 error->message);
      return;
    }

  /* This means the brightness got updated from the system and tried to
   * set it at the same time. Let's try to set the brightness the system
   * was setting to make sure we're in the correct state.
   */
  if (priv->brightness_target != brightness)
    {
      priv->pending = TRUE;

      META_BACKLIGHT_GET_CLASS (backlight)->set_brightness (backlight,
                                                            priv->brightness_target,
                                                            NULL,
                                                            on_brightness_set,
                                                            NULL);
    }
}

void
meta_backlight_set_brightness (MetaBacklight *backlight,
                               int            brightness)
{
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);
  int new_brightness;

  if (priv->brightness_target == brightness)
    return;

  new_brightness = CLAMP (brightness,
                          priv->brightness_min, priv->brightness_max);

  if (brightness != new_brightness)
    {
      g_warning ("Trying to set out-of-range brightness on %s (%s)",
                 priv->name,
                 g_type_name (G_OBJECT_TYPE (backlight)));
    }

  priv->brightness_target = new_brightness;
  g_object_notify_by_pspec (G_OBJECT (backlight), obj_props[PROP_BRIGHTNESS]);

  if (!priv->pending)
    {
      priv->pending = TRUE;

      META_BACKLIGHT_GET_CLASS (backlight)->set_brightness (backlight,
                                                            priv->brightness_target,
                                                            NULL,
                                                            on_brightness_set,
                                                            NULL);
    }
}

int
meta_backlight_get_brightness (MetaBacklight *backlight)
{
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);

  return priv->brightness_target;
}

void
meta_backlight_get_brightness_info (MetaBacklight *backlight,
                                    int           *brightness_min_out,
                                    int           *brightness_max_out)
{
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);

  if (brightness_min_out)
    *brightness_min_out = priv->brightness_min;
  if (brightness_max_out)
    *brightness_max_out = priv->brightness_max;
}

static void
meta_backlight_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  MetaBacklight *backlight = META_BACKLIGHT (object);
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_strdup (g_value_get_string (value));
      break;
    case PROP_BRIGHTNESS_MIN:
      priv->brightness_min = g_value_get_int (value);
      break;
    case PROP_BRIGHTNESS_MAX:
      priv->brightness_max = g_value_get_int (value);
      break;
    case PROP_BRIGHTNESS:
      meta_backlight_set_brightness (backlight, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_backlight_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  MetaBacklight *backlight = META_BACKLIGHT (object);
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_BRIGHTNESS_MIN:
      g_value_set_int (value, priv->brightness_min);
      break;
    case PROP_BRIGHTNESS_MAX:
      g_value_set_int (value, priv->brightness_max);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_int (value, priv->brightness_target);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_backlight_dispose (GObject *object)
{
  MetaBacklight *backlight = META_BACKLIGHT (object);
  MetaBacklightPrivate *priv = meta_backlight_get_instance_private (backlight);

  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (meta_backlight_parent_class)->dispose (object);
}

static void
meta_backlight_class_init (MetaBacklightClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_backlight_dispose;
  object_class->get_property = meta_backlight_get_property;
  object_class->set_property = meta_backlight_set_property;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         "unknown",
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_BRIGHTNESS_MIN] =
    g_param_spec_int ("brightness-min", NULL, NULL,
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);
  obj_props[PROP_BRIGHTNESS_MAX] =
    g_param_spec_int ("brightness-max", NULL, NULL,
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);
  obj_props[PROP_BRIGHTNESS] =
    g_param_spec_int ("brightness", NULL, NULL,
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_EXPLICIT_NOTIFY |
                      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}

static void
meta_backlight_init (MetaBacklight *backlight)
{
}
