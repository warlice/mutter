/*
 * Copyright (C) 2022 Red Hat
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

#include "backends/native/meta-frame-native.h"

#include "backends/native/meta-kms-update.h"
#include "clutter/clutter-mutter.h"
#include "core/util-private.h"

struct _MetaFrameNative
{
  ClutterFrame base;

  MetaDrmBuffer *buffer;
  CoglScanout *scanout;

  MetaKmsUpdate *kms_update;

  struct {
    int n_rectangles;
    int *rectangles;  /* 4 x n_rectangles */
  } damage;
};

static void
meta_frame_native_release (ClutterFrame *frame)
{
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);

  g_clear_pointer (&frame_native->damage.rectangles, g_free);
  g_clear_object (&frame_native->buffer);
  g_clear_object (&frame_native->scanout);

  g_return_if_fail (!frame_native->kms_update);
}

MetaFrameNative *
meta_frame_native_new (void)
{
  return clutter_frame_new (MetaFrameNative, meta_frame_native_release);
}

MetaFrameNative *
meta_frame_native_from_frame (ClutterFrame *frame)
{
  return META_CONTAINER_OF (frame, MetaFrameNative, base);
}

MetaKmsUpdate *
meta_frame_native_ensure_kms_update (MetaFrameNative *frame_native,
                                     MetaKmsDevice   *kms_device)
{
  if (frame_native->kms_update)
    {
      g_warn_if_fail (meta_kms_update_get_device (frame_native->kms_update) ==
                      kms_device);
      return frame_native->kms_update;
    }

  frame_native->kms_update = meta_kms_update_new (kms_device);
  return frame_native->kms_update;
}

MetaKmsUpdate *
meta_frame_native_steal_kms_update (MetaFrameNative *frame_native)
{
  return g_steal_pointer (&frame_native->kms_update);
}

gboolean
meta_frame_native_has_kms_update (MetaFrameNative *frame_native)
{
  return !!frame_native->kms_update;
}

void
meta_frame_native_set_buffer (MetaFrameNative *frame_native,
                              MetaDrmBuffer   *buffer)
{
  g_set_object (&frame_native->buffer, buffer);
}

MetaDrmBuffer *
meta_frame_native_get_buffer (MetaFrameNative *frame_native)
{
  return frame_native->buffer;
}

void
meta_frame_native_set_scanout (MetaFrameNative *frame_native,
                               CoglScanout     *scanout)
{
  g_set_object (&frame_native->scanout, scanout);
}

CoglScanout *
meta_frame_native_get_scanout (MetaFrameNative *frame_native)
{
  return frame_native->scanout;
}

void
meta_frame_native_set_damage (MetaFrameNative *frame_native,
                              const int       *rectangles,
                              int              n_rectangles)
{
  size_t rectangles_size;

  rectangles_size = n_rectangles * 4 * sizeof (int);

  frame_native->damage.rectangles =
    g_realloc (frame_native->damage.rectangles, rectangles_size);
  memcpy (frame_native->damage.rectangles, rectangles, rectangles_size);
  frame_native->damage.n_rectangles = n_rectangles;
}

int
meta_frame_native_get_damage (MetaFrameNative  *frame_native,
                              int             **rectangles)
{
  if (rectangles)
    *rectangles = frame_native->damage.rectangles;

  return frame_native->damage.n_rectangles;
}
