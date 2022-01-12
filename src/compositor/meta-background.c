/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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

#include "compositor/meta-background-private.h"

#include <string.h>

#include "backends/meta-backend-private.h"
#include "compositor/cogl-utils.h"
#include "meta/display.h"
#include "meta/meta-background-image.h"
#include "meta/meta-background.h"
#include "meta/meta-monitor-manager.h"
#include "meta/util.h"

enum
{
  CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct _MetaBackgroundMonitor MetaBackgroundMonitor;

struct _MetaBackgroundMonitor
{
  gboolean dirty;
  CoglTexture *texture;
  CoglFramebuffer *fbo;
};

struct _MetaBackground
{
  GObject parent;

  MetaDisplay *display;
  MetaBackgroundMonitor *monitors;
  int n_monitors;

  ClutterColor              color;

  GFile *file;
  MetaBackgroundImage *background_image;

  CoglTexture *color_texture;
};

enum
{
  PROP_META_DISPLAY = 1,
  PROP_MONITOR,
};

G_DEFINE_TYPE (MetaBackground, meta_background, G_TYPE_OBJECT)

static GSList *all_backgrounds = NULL;

static void
free_fbos (MetaBackground *self)
{
  int i;

  for (i = 0; i < self->n_monitors; i++)
    {
      MetaBackgroundMonitor *monitor = &self->monitors[i];

      g_clear_object (&monitor->fbo);
      cogl_clear_object (&monitor->texture);
    }
}

static void
free_color_texture (MetaBackground *self)
{
  cogl_clear_object (&self->color_texture);
}

static void
invalidate_monitor_backgrounds (MetaBackground *self)
{
  free_fbos (self);
  g_clear_pointer (&self->monitors, g_free);
  self->n_monitors = 0;

  if (self->display)
    {
      int i;

      self->n_monitors = meta_display_get_n_monitors (self->display);
      self->monitors = g_new0 (MetaBackgroundMonitor, self->n_monitors);

      for (i = 0; i < self->n_monitors; i++)
        self->monitors[i].dirty = TRUE;
    }
}

static void
on_monitors_changed (MetaBackground *self)
{
  invalidate_monitor_backgrounds (self);
}

static void
set_display (MetaBackground *self,
             MetaDisplay    *display)
{
  g_set_object (&self->display, display);

  invalidate_monitor_backgrounds (self);
}

static void
meta_background_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      set_display (META_BACKGROUND (object), g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_get_property (GObject      *object,
                              guint         prop_id,
                              GValue       *value,
                              GParamSpec   *pspec)
{
  MetaBackground *self = META_BACKGROUND (object);

  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      g_value_set_object (value, self->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
need_prerender (MetaBackground *self)
{
  CoglTexture *texture = self->background_image ? meta_background_image_get_texture (self->background_image) : NULL;

  if (texture == NULL)
    return FALSE;

  return TRUE;
}

static void
mark_changed (MetaBackground *self)
{
  int i;

  if (!need_prerender (self))
    free_fbos (self);

  for (i = 0; i < self->n_monitors; i++)
    self->monitors[i].dirty = TRUE;

  g_signal_emit (self, signals[CHANGED], 0);
}

static void
on_background_loaded (MetaBackgroundImage *image,
                      MetaBackground      *self)
{
  mark_changed (self);
}

static gboolean
file_equal0 (GFile *file1,
             GFile *file2)
{
  if (file1 == file2)
    return TRUE;

  if ((file1 == NULL) || (file2 == NULL))
    return FALSE;

  return g_file_equal (file1, file2);
}

static void
set_file (MetaBackground       *self,
          GFile               **filep,
          MetaBackgroundImage **imagep,
          GFile                *file,
          gboolean              force_reload)
{
  if (force_reload || !file_equal0 (*filep, file))
    {
      if (*imagep)
        {
          g_signal_handlers_disconnect_by_func (*imagep,
                                                (gpointer)on_background_loaded,
                                                self);
          g_clear_object (imagep);
        }

      g_set_object (filep, file);

      if (file)
        {
          MetaBackgroundImageCache *cache = meta_background_image_cache_get_default ();

          *imagep = meta_background_image_cache_load (cache, file);
          g_signal_connect (*imagep, "loaded",
                            G_CALLBACK (on_background_loaded), self);
        }
    }
}

static void
on_gl_video_memory_purged (MetaBackground *self)
{
  MetaBackgroundImageCache *cache = meta_background_image_cache_get_default ();

  /* The GPU memory that just got invalidated is the texture inside
   * self->background_image and/or its mipmaps. However, to save memory the
   * original pixbuf isn't kept in RAM so we can't do a simple re-upload. The
   * only copy of the image was the one in texture memory that got invalidated.
   * So we need to do a full reload from disk.
   */
  if (self->file)
    {
      meta_background_image_cache_purge (cache, self->file);
      set_file (self, &self->file, &self->background_image, self->file, TRUE);
    }

  mark_changed (self);
}

static void
meta_background_dispose (GObject *object)
{
  MetaBackground        *self = META_BACKGROUND (object);

  free_color_texture (self);

  set_file (self, &self->file, &self->background_image, NULL, FALSE);

  set_display (self, NULL);

  G_OBJECT_CLASS (meta_background_parent_class)->dispose (object);
}

static void
meta_background_finalize (GObject *object)
{
  all_backgrounds = g_slist_remove (all_backgrounds, object);

  G_OBJECT_CLASS (meta_background_parent_class)->finalize (object);
}

static void
meta_background_constructed (GObject *object)
{
  MetaBackground        *self = META_BACKGROUND (object);
  MetaMonitorManager *monitor_manager = meta_monitor_manager_get ();

  G_OBJECT_CLASS (meta_background_parent_class)->constructed (object);

  g_signal_connect_object (self->display, "gl-video-memory-purged",
                           G_CALLBACK (on_gl_video_memory_purged), object, G_CONNECT_SWAPPED);

  g_signal_connect_object (monitor_manager, "monitors-changed",
                           G_CALLBACK (on_monitors_changed), self,
                           G_CONNECT_SWAPPED);
}

static void
meta_background_class_init (MetaBackgroundClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->dispose = meta_background_dispose;
  object_class->finalize = meta_background_finalize;
  object_class->constructed = meta_background_constructed;
  object_class->set_property = meta_background_set_property;
  object_class->get_property = meta_background_get_property;

  signals[CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  param_spec = g_param_spec_object ("meta-display",
                                    "MetaDisplay",
                                    "MetaDisplay",
                                    META_TYPE_DISPLAY,
                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_META_DISPLAY,
                                   param_spec);

}

static void
meta_background_init (MetaBackground *self)
{
  all_backgrounds = g_slist_prepend (all_backgrounds, self);
}

static void
set_texture_area_from_monitor_area (cairo_rectangle_int_t *monitor_area,
                                    cairo_rectangle_int_t *texture_area)
{
  texture_area->x = 0;
  texture_area->y = 0;
  texture_area->width = monitor_area->width;
  texture_area->height = monitor_area->height;
}

static void
get_texture_area (MetaBackground          *self,
                  cairo_rectangle_int_t   *monitor_rect,
                  float                    monitor_scale,
                  CoglTexture             *texture,
                  cairo_rectangle_int_t   *texture_area)
{
  cairo_rectangle_int_t image_area;
  float texture_width, texture_height;
  float monitor_x_scale, monitor_y_scale;

  texture_width = cogl_texture_get_width (texture);
  texture_height = cogl_texture_get_height (texture);

  monitor_x_scale = monitor_rect->width / texture_width;
  monitor_y_scale = monitor_rect->height / texture_height;

  if (monitor_x_scale > monitor_y_scale)
    {
      /* Fill image to exactly fit actor horizontally */
      image_area.width = monitor_rect->width;
      image_area.height = texture_height * monitor_x_scale;

      /* Position image centered vertically in actor */
      image_area.x = 0;
      image_area.y = monitor_rect->height / 2 - image_area.height / 2;
    }
  else
    {
      /* Scale image to exactly fit actor vertically */
      image_area.width = texture_width * monitor_y_scale;
      image_area.height = monitor_rect->height;

      /* Position image centered horizontally in actor */
      image_area.x = monitor_rect->width / 2 - image_area.width / 2;
      image_area.y = 0;
    }

  *texture_area = image_area;
}

static void
draw_texture (MetaBackground        *self,
              CoglFramebuffer       *framebuffer,
              CoglPipeline          *pipeline,
              CoglTexture           *texture,
              cairo_rectangle_int_t *monitor_area,
              float                  monitor_scale)
{
  cairo_rectangle_int_t texture_area;

  get_texture_area (self, monitor_area, monitor_scale, texture, &texture_area);

  cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                            pipeline,
                                            0,
                                            0,
                                            monitor_area->width,
                                            monitor_area->height,
                                            - texture_area.x / (float)texture_area.width,
                                            - texture_area.y / (float)texture_area.height,
                                            (monitor_area->width - texture_area.x) / (float)texture_area.width,
                                            (monitor_area->height - texture_area.y) / (float)texture_area.height);
}

static void
ensure_color_texture (MetaBackground *self)
{
  if (self->color_texture == NULL)
    {
      ClutterBackend *backend = clutter_get_default_backend ();
      CoglContext *ctx = clutter_backend_get_cogl_context (backend);
      GError *error = NULL;
      uint8_t pixels[6];
      int width, height;

      width = 1;
      height = 1;

      pixels[0] = self->color.red;
      pixels[1] = self->color.green;
      pixels[2] = self->color.blue;

      self->color_texture = COGL_TEXTURE (cogl_texture_2d_new_from_data (ctx, width, height,
                                                                         COGL_PIXEL_FORMAT_RGB_888,
                                                                         width * 3,
                                                                         pixels,
                                                                         &error));

      if (error != NULL)
        {
          meta_warning ("Failed to allocate color texture: %s", error->message);
          g_error_free (error);
        }
    }
}

static CoglPipeline *
create_pipeline (void)
{
  static CoglPipeline *pipeline = NULL;

  if (pipeline == NULL)
      pipeline = meta_create_texture_pipeline (NULL);

  cogl_pipeline_set_layer_filters (pipeline,
                                   0,
                                   COGL_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);

  return cogl_pipeline_copy (pipeline);
}

static int
get_best_mipmap_level (CoglTexture *texture,
                       int          visible_width,
                       int          visible_height)
{
  int mipmap_width = cogl_texture_get_width (texture);
  int mipmap_height = cogl_texture_get_height (texture);
  int halves = 0;

  while (mipmap_width >= visible_width && mipmap_height >= visible_height)
    {
      halves++;
      mipmap_width /= 2;
      mipmap_height /= 2;
    }

  return MAX (0, halves - 1);
}

CoglTexture *
meta_background_get_texture (MetaBackground         *self,
                             int                     monitor_index,
                             cairo_rectangle_int_t  *texture_area,
                             CoglPipelineWrapMode   *wrap_mode)
{
  MetaBackgroundMonitor *monitor;
  MetaRectangle geometry;
  cairo_rectangle_int_t monitor_area;
  CoglTexture *texture;
  float monitor_scale;

  g_return_val_if_fail (META_IS_BACKGROUND (self), NULL);
  g_return_val_if_fail (monitor_index >= 0 && monitor_index < self->n_monitors, NULL);

  monitor = &self->monitors[monitor_index];

  meta_display_get_monitor_geometry (self->display, monitor_index, &geometry);
  monitor_scale = meta_display_get_monitor_scale (self->display, monitor_index);
  monitor_area.x = geometry.x;
  monitor_area.y = geometry.y;
  monitor_area.width = geometry.width;
  monitor_area.height = geometry.height;

  texture = self->background_image ? meta_background_image_get_texture (self->background_image) : NULL;

  if (texture == NULL)
    {
      ensure_color_texture (self);
      if (texture_area)
        set_texture_area_from_monitor_area (&monitor_area, texture_area);
      if (wrap_mode)
        *wrap_mode = COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE;
      return self->color_texture;
    }

  if (monitor->dirty)
    {
      GError *catch_error = NULL;
      int texture_width, texture_height;

      if (meta_is_stage_views_scaled ())
        {
          texture_width = monitor_area.width * monitor_scale;
          texture_height = monitor_area.height * monitor_scale;
        }
      else
        {
          texture_width = monitor_area.width;
          texture_height = monitor_area.height;
        }

      if (monitor->texture == NULL)
        {
          CoglOffscreen *offscreen;

          monitor->texture = meta_create_texture (texture_width,
                                                  texture_height,
                                                  COGL_TEXTURE_COMPONENTS_RGB,
                                                  META_TEXTURE_FLAGS_NONE);
          offscreen = cogl_offscreen_new_with_texture (monitor->texture);
          monitor->fbo = COGL_FRAMEBUFFER (offscreen);
        }

      monitor_area.x *= monitor_scale;
      monitor_area.y *= monitor_scale;
      monitor_area.width *= monitor_scale;
      monitor_area.height *= monitor_scale;

      if (!cogl_framebuffer_allocate (monitor->fbo, &catch_error))
        {
          /* Texture or framebuffer allocation failed; it's unclear why this happened;
           * we'll try again the next time this is called. (MetaBackgroundActor
           * caches the result, so user might be left without a background.)
           */
          cogl_clear_object (&monitor->texture);
          g_clear_object (&monitor->fbo);

          g_error_free (catch_error);
          return NULL;
        }

      cogl_framebuffer_orthographic (monitor->fbo, 0, 0,
                                     monitor_area.width, monitor_area.height, -1., 1.);

      cogl_framebuffer_clear4f (monitor->fbo,
                                COGL_BUFFER_BIT_COLOR,
                                0.0, 0.0, 0.0, 0.0);

      if (texture != NULL)
        {
          CoglPipeline *pipeline = create_pipeline ();
          int mipmap_level;

          mipmap_level = get_best_mipmap_level (texture,
                                                texture_width,
                                                texture_height);

          cogl_pipeline_set_color4f (pipeline, 1.0, 1.0, 1.0, 1.0);
          cogl_pipeline_set_layer_texture (pipeline, 0, texture);
          cogl_pipeline_set_layer_wrap_mode (pipeline, 0, COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
          cogl_pipeline_set_layer_max_mipmap_level (pipeline, 0, mipmap_level);

          draw_texture (self, monitor->fbo, pipeline,
                        texture, &monitor_area, monitor_scale);

          cogl_object_unref (pipeline);
        }

      monitor->dirty = FALSE;
    }

  if (texture_area)
    set_texture_area_from_monitor_area (&geometry, texture_area);

  if (wrap_mode)
    *wrap_mode = COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE;
  return monitor->texture;
}

MetaBackground *
meta_background_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_BACKGROUND,
                       "meta-display", display,
                       NULL);
}

void
meta_background_set_color (MetaBackground *self,
                           ClutterColor   *color)
{
  g_return_if_fail (META_IS_BACKGROUND (self));
  g_return_if_fail (color != NULL);

  self->color = *color;

  free_color_texture (self);
  mark_changed (self);
}

/**
 * meta_background_set_file:
 * @self: a #MetaBackground
 * @file: (nullable): a #GFile representing the background file
 *
 * Set the background to @file
 */
void
meta_background_set_file (MetaBackground            *self,
                          GFile                     *file)
{
  g_return_if_fail (META_IS_BACKGROUND (self));

  set_file (self, &self->file, &self->background_image, file, FALSE);

  mark_changed (self);
}

void
meta_background_refresh_all (void)
{
  GSList *l;

  for (l = all_backgrounds; l; l = l->next)
    mark_changed (l->data);
}
