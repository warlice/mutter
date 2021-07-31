/*
 * Copyright (C) 2018-2021 Red Hat Inc.
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

/* Partly based on pipewire-media-stream by Georges Basile Stavracas Neto. */

#include "config.h"

#include "mdk-stream.h"

#include <drm/drm_fourcc.h>
#include <epoxy/egl.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <linux/dma-buf.h>
#include <pipewire/stream.h>
#include <spa/debug/format.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/hook.h>
#include <sys/mman.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#endif

#include "mdk-context.h"
#include "mdk-pipewire.h"
#include "mdk-session.h"

#include "mdk-dbus-screen-cast.h"

enum
{
  ERROR,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MdkStream
{
  GObject parent;

  MdkSession *session;
  int width;
  int height;

  GCancellable *init_cancellable;

  MdkDBusScreenCastStream *proxy;

  GdkGLContext *gl_context;
  GdkPaintable *paintable;

  uint32_t node_id;
  struct pw_stream *pipewire_stream;
  struct spa_hook pipewire_stream_listener;

  struct spa_video_info format;
};

#define CURSOR_META_SIZE(width, height) \
  (sizeof(struct spa_meta_cursor) + \
   sizeof(struct spa_meta_bitmap) + width * height * 4)

static void paintable_iface_init (GdkPaintableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MdkStream, mdk_stream, GTK_TYPE_MEDIA_STREAM,
                         G_IMPLEMENT_INTERFACE (GDK_TYPE_PAINTABLE,
                                                paintable_iface_init))

static gboolean
spa_pixel_format_to_gdk_memory_format (uint32_t         spa_format,
                                       GdkMemoryFormat *out_format,
                                       uint32_t        *out_bpp)
{
  switch (spa_format)
    {
    case SPA_VIDEO_FORMAT_RGBA:
    case SPA_VIDEO_FORMAT_RGBx:
      *out_format = GDK_MEMORY_R8G8B8A8;
      *out_bpp = 4;
      break;
    case SPA_VIDEO_FORMAT_BGRA:
    case SPA_VIDEO_FORMAT_BGRx:
      *out_format = GDK_MEMORY_B8G8R8A8;
      *out_bpp = 4;
      break;
    default:
      return FALSE;
    }

  return TRUE;
}

static gboolean
spa_pixel_format_to_drm_format (uint32_t  spa_format,
                                uint32_t *out_format)
{
  switch (spa_format)
    {
    case SPA_VIDEO_FORMAT_RGBA:
      *out_format = DRM_FORMAT_ABGR8888;
      break;
    case SPA_VIDEO_FORMAT_RGBx:
      *out_format = DRM_FORMAT_XBGR8888;
      break;
    case SPA_VIDEO_FORMAT_BGRA:
      *out_format = DRM_FORMAT_ARGB8888;
      break;
    case SPA_VIDEO_FORMAT_BGRx:
      *out_format = DRM_FORMAT_XRGB8888;
      break;
    default:
      return FALSE;
    }

  return TRUE;
}

static void
on_stream_state_changed (void                 *user_data,
                         enum pw_stream_state  old,
                         enum pw_stream_state  state,
                         const char           *error)
{
  g_debug ("Pipewire stream state changed from %s to %s",
           pw_stream_state_as_string (old),
           pw_stream_state_as_string (state));

  switch (state)
    {
    case PW_STREAM_STATE_ERROR:
      g_warning ("PipeWire stream error: %s", error);
      break;
    case PW_STREAM_STATE_PAUSED:
    case PW_STREAM_STATE_STREAMING:
    case PW_STREAM_STATE_UNCONNECTED:
    case PW_STREAM_STATE_CONNECTING:
      break;
    }
}

static void
on_stream_param_changed (void                 *user_data,
                         uint32_t              id,
                         const struct spa_pod *format)
{
  MdkStream *stream = MDK_STREAM (user_data);
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  const struct spa_pod *params[3];
  int result;

  if (!format || id != SPA_PARAM_Format)
    return;

  result = spa_format_parse (format,
                             &stream->format.media_type,
                             &stream->format.media_subtype);
  if (result < 0)
    return;

  if (stream->format.media_type != SPA_MEDIA_TYPE_video ||
      stream->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
    return;

  spa_format_video_raw_parse (format, &stream->format.info.raw);

  g_debug ("Negotiated format:");
  g_debug ("     Format: %d (%s)",
           stream->format.info.raw.format,
           spa_debug_type_find_name (spa_type_video_format,
                                     stream->format.info.raw.format));
  g_debug ("     Size: %dx%d",
           stream->format.info.raw.size.width,
           stream->format.info.raw.size.height);
  g_debug ("     Framerate: %d/%d",
           stream->format.info.raw.framerate.num,
           stream->format.info.raw.framerate.denom);

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));

  params[0] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int (4, 1, 4),
    SPA_PARAM_BUFFERS_dataType, SPA_POD_Int ((1 << SPA_DATA_MemFd) |
                                             (1 << SPA_DATA_DmaBuf)),
    0);

  params[1] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Header),
    SPA_PARAM_META_size, SPA_POD_Int (sizeof (struct spa_meta_header)),
    0);

  params[2] = spa_pod_builder_add_object(
    &pod_builder,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Cursor),
    SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int (CURSOR_META_SIZE (384, 384),
                                                   CURSOR_META_SIZE (1,1),
                                                   CURSOR_META_SIZE (384, 384)),
    0);

  pw_stream_update_params (stream->pipewire_stream,
                           params, G_N_ELEMENTS (params));
}

static GdkPaintable *
import_dmabuf_egl (GdkGLContext   *context,
                   uint32_t        format,
                   unsigned int    width,
                   unsigned int    height,
                   uint32_t        n_planes,
                   const int      *fds,
                   const uint32_t *strides,
                   const uint32_t *offsets,
                   const uint64_t *modifiers)
{
  GdkDisplay *display;
  EGLDisplay egl_display = EGL_NO_DISPLAY;
  EGLint attribs[2 * (3 + 4 * 5) + 1];
  EGLImage image;
  guint texture_id;
  int i;

  display = gdk_gl_context_get_display (context);
  egl_display = NULL;

#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY (display))
    egl_display = gdk_wayland_display_get_egl_display (display);
#endif
#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11_DISPLAY (display))
    egl_display = gdk_x11_display_get_egl_display (display);
#endif

  if (egl_display == EGL_NO_DISPLAY)
    {
      g_warning ("Can't import DMA-BUF when not using EGL");
      return NULL;
    }

  i = 0;
  attribs[i++] = EGL_WIDTH;
  attribs[i++] = width;
  attribs[i++] = EGL_HEIGHT;
  attribs[i++] = height;
  attribs[i++] = EGL_LINUX_DRM_FOURCC_EXT;
  attribs[i++] = format;

  if (n_planes > 0)
    {
      attribs[i++] = EGL_DMA_BUF_PLANE0_FD_EXT;
      attribs[i++] = fds[0];
      attribs[i++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
      attribs[i++] = offsets[0];
      attribs[i++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
      attribs[i++] = strides[0];
      if (modifiers)
        {
          attribs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
          attribs[i++] = modifiers[0] & 0xFFFFFFFF;
          attribs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
          attribs[i++] = modifiers[0] >> 32;
        }
    }

  if (n_planes > 1)
    {
      attribs[i++] = EGL_DMA_BUF_PLANE1_FD_EXT;
      attribs[i++] = fds[1];
      attribs[i++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
      attribs[i++] = offsets[1];
      attribs[i++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
      attribs[i++] = strides[1];
      if (modifiers)
        {
          attribs[i++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
          attribs[i++] = modifiers[1] & 0xFFFFFFFF;
          attribs[i++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
          attribs[i++] = modifiers[1] >> 32;
        }
    }

  if (n_planes > 2)
    {
      attribs[i++] = EGL_DMA_BUF_PLANE2_FD_EXT;
      attribs[i++] = fds[2];
      attribs[i++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
      attribs[i++] = offsets[2];
      attribs[i++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
      attribs[i++] = strides[2];
      if (modifiers)
        {
          attribs[i++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
          attribs[i++] = modifiers[2] & 0xFFFFFFFF;
          attribs[i++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
          attribs[i++] = modifiers[2] >> 32;
        }
    }
  if (n_planes > 3)
    {
      attribs[i++] = EGL_DMA_BUF_PLANE3_FD_EXT;
      attribs[i++] = fds[3];
      attribs[i++] = EGL_DMA_BUF_PLANE3_OFFSET_EXT;
      attribs[i++] = offsets[3];
      attribs[i++] = EGL_DMA_BUF_PLANE3_PITCH_EXT;
      attribs[i++] = strides[3];
      if (modifiers)
        {
          attribs[i++] = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
          attribs[i++] = modifiers[3] & 0xFFFFFFFF;
          attribs[i++] = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
          attribs[i++] = modifiers[3] >> 32;
        }
    }

  attribs[i++] = EGL_NONE;

  image = eglCreateImageKHR (egl_display,
                             EGL_NO_CONTEXT,
                             EGL_LINUX_DMA_BUF_EXT,
                             (EGLClientBuffer)NULL,
                             attribs);
  if (image == EGL_NO_IMAGE)
    {
      g_warning ("Failed to create EGL image: %d\n", eglGetError ());
      return 0;
    }

  gdk_gl_context_make_current (context);

  glGenTextures (1, &texture_id);
  glBindTexture (GL_TEXTURE_2D, texture_id);
  glEGLImageTargetTexture2DOES (GL_TEXTURE_2D, image);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  eglDestroyImageKHR (egl_display, image);

  return GDK_PAINTABLE (gdk_gl_texture_new (context,
                                            texture_id,
                                            width, height,
                                            NULL, NULL));
}

static void
on_stream_process (void *user_data)
{
  MdkStream *stream = MDK_STREAM (user_data);
  struct pw_buffer *next_buffer;
  struct pw_buffer *buffer = NULL;
  struct spa_buffer *spa_buffer;
  gboolean invalidated;
  gboolean has_buffer;
  uint32_t drm_format;
  
  next_buffer = pw_stream_dequeue_buffer (stream->pipewire_stream);
  while (next_buffer)
    {
      buffer = next_buffer;
      next_buffer = pw_stream_dequeue_buffer (stream->pipewire_stream);

      if (next_buffer)
        pw_stream_queue_buffer (stream->pipewire_stream, buffer);
    }
  if (!buffer)
    return;

  spa_buffer = buffer->buffer;
  has_buffer = spa_buffer->datas[0].chunk->size != 0;
  invalidated = FALSE;

  if (!has_buffer)
    goto done;

  if (spa_buffer->datas[0].type == SPA_DATA_DmaBuf)
    {
      uint32_t *offsets;
      uint32_t *strides;
      uint64_t *modifiers;
      uint32_t n_datas;
      unsigned int i;
      int *fds;

      if (!spa_pixel_format_to_drm_format (stream->format.info.raw.format,
                                           &drm_format))
        {
          g_critical ("Unsupported DMA buffer format: %d",
                      stream->format.info.raw.format);
          goto done;
        }

      n_datas = spa_buffer->n_datas;
      fds = g_alloca (sizeof (int) * n_datas);
      offsets = g_alloca (sizeof (uint32_t) * n_datas);
      strides = g_alloca (sizeof (uint32_t) * n_datas);

      modifiers = g_alloca (sizeof (uint64_t) * n_datas);
      for (i = 0; i < n_datas; i++)
        {
          fds[i] = spa_buffer->datas[i].fd;
          offsets[i] = spa_buffer->datas[i].chunk->offset;
          strides[i] = spa_buffer->datas[i].chunk->stride;
          modifiers[i] = stream->format.info.raw.modifier;
        }

      g_clear_object (&stream->paintable);
      stream->paintable = import_dmabuf_egl (stream->gl_context,
                                             drm_format,
                                             stream->format.info.raw.size.width,
                                             stream->format.info.raw.size.height,
                                             1,
                                             fds,
                                             strides,
                                             offsets,
                                             modifiers);
      invalidated = TRUE;
    }
  else
    {
      g_autoptr (GdkTexture) texture = NULL;
      g_autoptr (GBytes) bytes = NULL;
      GdkMemoryFormat gdk_format;
      uint8_t *map;
      void *data;
      uint32_t bpp;
      size_t size;

      if (!spa_pixel_format_to_gdk_memory_format (stream->format.info.raw.format,
                                                  &gdk_format,
                                                  &bpp))
        {
          g_critical ("Unsupported memory buffer format: %d",
                      stream->format.info.raw.format);
          goto done;
        }

      size = spa_buffer->datas[0].maxsize + spa_buffer->datas[0].mapoffset;

      map = mmap (NULL, size, PROT_READ, MAP_PRIVATE, spa_buffer->datas[0].fd, 0);
      if (map == MAP_FAILED)
        {
          g_critical ("Failed to mmap buffer: %s", g_strerror (errno));
          goto done;
        }
      data = SPA_MEMBER (map, spa_buffer->datas[0].mapoffset, uint8_t);

      bytes = g_bytes_new (data, size);

      texture = gdk_memory_texture_new (stream->format.info.raw.size.width,
                                        stream->format.info.raw.size.height,
                                        gdk_format,
                                        bytes,
                                        spa_buffer->datas[0].chunk->stride);
      g_set_object (&stream->paintable, GDK_PAINTABLE (texture));

      munmap (map, size);

      invalidated = TRUE;
    }

done:

  if (invalidated)
    gdk_paintable_invalidate_contents (GDK_PAINTABLE (stream));

  pw_stream_queue_buffer (stream->pipewire_stream, buffer);
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .param_changed = on_stream_param_changed,
  .process = on_stream_process,
};

static gboolean
connect_to_stream (MdkStream  *stream,
                   GError    **error)
{
  MdkSession *session = mdk_stream_get_session (stream);
  MdkContext *context = mdk_session_get_context (session);
  MdkPipewire *pipewire = mdk_context_get_pipewire (context);
  struct pw_stream *pipewire_stream;
  uint8_t params_buffer[1024];
  struct spa_pod_builder pod_builder;
  struct spa_rectangle rect;
  struct spa_fraction min_framerate;
  struct spa_fraction max_framerate;
  const struct spa_pod *params[2];
  int ret;

  pipewire_stream = pw_stream_new (mdk_pipewire_get_core (pipewire),
                                   "mutter-sdk-pipewire-stream",
                                   NULL);

  rect = SPA_RECTANGLE (stream->width, stream->height);
  min_framerate = SPA_FRACTION (1, 1);
  max_framerate = SPA_FRACTION (60, 1);

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));
  params[0] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
    SPA_FORMAT_mediaType, SPA_POD_Id (SPA_MEDIA_TYPE_video),
    SPA_FORMAT_mediaSubtype, SPA_POD_Id (SPA_MEDIA_SUBTYPE_raw),
    SPA_FORMAT_VIDEO_format, SPA_POD_Id (SPA_VIDEO_FORMAT_BGRx),
    SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle (&rect),
    SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction (&SPA_FRACTION (0, 1)),
    SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction (&min_framerate,
                                                                  &min_framerate,
                                                                  &max_framerate),
    0);

  stream->pipewire_stream = pipewire_stream;

  pw_stream_add_listener (pipewire_stream,
                          &stream->pipewire_stream_listener,
                          &stream_events,
                          stream);

  ret = pw_stream_connect (stream->pipewire_stream,
                           PW_DIRECTION_INPUT,
                           stream->node_id,
                           PW_STREAM_FLAG_AUTOCONNECT,
                           params, 1);
  if (ret < 0)
    {
      g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                           strerror (-ret));
      return FALSE;
    }

  return TRUE;
}

static void
on_pipewire_stream_added (MdkDBusScreenCastStream *proxy,
                          unsigned int             node_id,
                          MdkStream               *stream)
{
  g_autoptr (GError) error = NULL;

  stream->node_id = (uint32_t) node_id;

  g_debug ("Received PipeWire stream node %u, connecting", node_id);

  if (!connect_to_stream (stream, &error))
    {
      g_signal_emit (stream, signals[ERROR], 0, error);
      return;
    }
}

static void
start_cb (GObject      *source_object,
          GAsyncResult *res,
          gpointer      user_data)
{
  MdkStream *stream = MDK_STREAM (user_data);
  g_autoptr (GError) error = NULL;

  if (!mdk_dbus_screen_cast_stream_call_start_finish (stream->proxy,
                                                      res,
                                                      &error))
    {
      g_signal_emit (stream, signals[ERROR], 0, error);
      return;
    }
}

static void
stream_proxy_ready_cb (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  MdkStream *stream = user_data;
  g_autoptr (GError) error = NULL;

  stream->proxy =
    mdk_dbus_screen_cast_stream_proxy_new_for_bus_finish (res, &error);
  if (!stream->proxy)
    {
      g_signal_emit (stream, signals[ERROR], 0, error);
      return;
    }

  g_debug ("Stream ready, waiting for PipeWire stream node");

  g_signal_connect (stream->proxy, "pipewire-stream-added",
                    G_CALLBACK (on_pipewire_stream_added),
                    stream);

  mdk_dbus_screen_cast_stream_call_start (stream->proxy,
                                          NULL,
                                          start_cb,
                                          stream);
}

static void
mdk_stream_paintable_snapshot (GdkPaintable *paintable,
                               GdkSnapshot  *snapshot,
                               double        width,
                               double        height)
{
  MdkStream *stream = MDK_STREAM (paintable);

  if (!stream->gl_context)
    return;

  if (stream->paintable)
    {
      gdk_paintable_snapshot (stream->paintable,
                              snapshot,
                              width,
                              height);
    }
}

static GdkPaintable *
mdk_stream_paintable_get_current_image (GdkPaintable *paintable)
{
  MdkStream *stream = MDK_STREAM (paintable);

  if (stream->paintable)
    return g_object_ref (stream->paintable);
  else
    return gdk_paintable_new_empty (stream->width, stream->height);
}

static int
mdk_stream_paintable_get_intrinsic_width (GdkPaintable *paintable)
{
  MdkStream *stream = MDK_STREAM (paintable);

  return stream->width;
}

static int
mdk_stream_paintable_get_intrinsic_height (GdkPaintable *paintable)
{
  MdkStream *stream = MDK_STREAM (paintable);

  return stream->height;
}

static double
mdk_stream_paintable_get_intrinsic_aspect_ratio (GdkPaintable *paintable)
{
  MdkStream *stream = MDK_STREAM (paintable);

  return (double) stream->width / (double) stream->height;
};

static void
paintable_iface_init (GdkPaintableInterface *iface)
{
  iface->snapshot = mdk_stream_paintable_snapshot;
  iface->get_current_image = mdk_stream_paintable_get_current_image;
  iface->get_intrinsic_width = mdk_stream_paintable_get_intrinsic_width;
  iface->get_intrinsic_height = mdk_stream_paintable_get_intrinsic_height;
  iface->get_intrinsic_aspect_ratio = mdk_stream_paintable_get_intrinsic_aspect_ratio;
}

static void
mdk_stream_finalize (GObject *object)
{
  MdkStream *stream = MDK_STREAM (object);

  g_clear_object (&stream->proxy);

  G_OBJECT_CLASS (mdk_stream_parent_class)->finalize (object);
}

static void
mdk_stream_class_init (MdkStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_stream_finalize;

  signals[ERROR] = g_signal_new ("error",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 1,
                                 G_TYPE_ERROR);
}

static void
mdk_stream_init (MdkStream *stream)
{
}

static void
create_monitor_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  MdkStream *stream = MDK_STREAM (user_data);
  g_autoptr (GError) error = NULL;
  const char *stream_path;

  stream_path = mdk_session_create_monitor_finish (stream->session,
                                                   res,
                                                   &error);
  if (!stream_path)
    g_signal_emit (stream, signals[ERROR], 0, error);

  g_debug ("Creating stream proxy for '%s'", stream_path);

  mdk_dbus_screen_cast_stream_proxy_new_for_bus (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.Mutter.ScreenCast",
    stream_path,
    stream->init_cancellable,
    stream_proxy_ready_cb,
    stream);
}

static void
init_async (MdkStream *stream)
{
  GCancellable *cancellable;

  cancellable = g_cancellable_new ();
  stream->init_cancellable = cancellable;

  mdk_session_create_monitor_async (stream->session,
                                    cancellable,
                                    create_monitor_cb,
                                    stream);
}

MdkStream *
mdk_stream_new (MdkSession *session,
                int         width,
                int         height)
{
  MdkStream *stream;

  stream = g_object_new (MDK_TYPE_STREAM, NULL);
  stream->session = session;
  stream->width = width;
  stream->height = height;

  init_async (stream);

  return stream;
}

MdkSession *
mdk_stream_get_session (MdkStream *stream)
{
  return stream->session;
}

void
mdk_stream_realize (MdkStream  *stream,
                    GdkSurface *surface)
{
  g_autoptr (GError) error = NULL;

  stream->gl_context = gdk_surface_create_gl_context (surface, &error);
  if (!stream->gl_context)
    {
      g_critical ("Failed to create GDK GL context: %s", error->message);
      return;
    }

  if (!gdk_gl_context_realize (stream->gl_context, &error))
    {
      g_critical ("Failed to realize GDK GL context: %s", error->message);
      g_clear_object (&stream->gl_context);
      return;
    }
}

void
mdk_stream_unrealize (MdkStream  *stream,
                      GdkSurface *surface)
{
  if (!stream->gl_context)
    return;

  if (gdk_gl_context_get_surface (stream->gl_context) == surface)
    g_clear_object (&stream->gl_context);
}

void
mdk_stream_map (MdkStream *stream)
{
  if (stream->pipewire_stream)
    pw_stream_set_active (stream->pipewire_stream, TRUE);
}

void
mdk_stream_unmap (MdkStream *stream)
{
  if (stream->pipewire_stream)
    pw_stream_set_active (stream->pipewire_stream, FALSE);
}
