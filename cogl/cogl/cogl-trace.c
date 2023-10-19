/*
 * Copyright 2018 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "config.h"

// Must come before cogl-trace.h.
#ifdef HAVE_TRACY
#include <tracy/TracyC.h>
#endif

#include "cogl/cogl-trace.h"

#ifdef HAVE_PROFILER

#include <sysprof-capture.h>
#include <sysprof-capture-writer.h>
#include <sysprof-clock.h>
#include <syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define COGL_TRACE_OUTPUT_FILE "cogl-trace-sp-capture.syscap"
#define BUFFER_LENGTH (4096 * 4)

struct _CoglTraceContext
{
  gatomicrefcount ref_count;
  SysprofCaptureWriter *writer;
};

typedef struct _CoglTraceThreadContext
{
  int cpu_id;
  GPid pid;
  char *group;
  CoglTraceContext *trace_context;
} CoglTraceThreadContext;

typedef struct
{
  char *group;
  CoglTraceContext *trace_context;
} TraceData;

static void cogl_trace_context_unref (CoglTraceContext *trace_context);

static void
trace_data_free (gpointer user_data)
{
  TraceData *data = user_data;

  g_clear_pointer (&data->group, g_free);
  g_clear_pointer (&data->trace_context, cogl_trace_context_unref);
  g_free (data);
}

static void cogl_trace_thread_context_free (gpointer data);

GPrivate cogl_trace_thread_data = G_PRIVATE_INIT (cogl_trace_thread_context_free);
CoglTraceContext *cogl_trace_context;
GMutex cogl_trace_mutex;

static CoglTraceContext *
cogl_trace_context_new (int         fd,
                        const char *filename)
{
  CoglTraceContext *context;
  SysprofCaptureWriter *writer;

  if (fd != -1)
    {
      g_debug ("Initializing trace context with fd=%d", fd);
      writer = sysprof_capture_writer_new_from_fd (fd, BUFFER_LENGTH);
    }
  else if (filename != NULL)
    {
      g_debug ("Initializing trace context with filename='%s'", filename);
      writer = sysprof_capture_writer_new (filename, BUFFER_LENGTH);
    }
  else
    {
      g_debug ("Initializing trace context with default filename");
      writer = sysprof_capture_writer_new (COGL_TRACE_OUTPUT_FILE, BUFFER_LENGTH);
    }

  if (!writer)
    return NULL;

  context = g_new0 (CoglTraceContext, 1);
  context->writer = writer;
  g_atomic_ref_count_init (&context->ref_count);
  return context;
}

static void
cogl_trace_context_unref (CoglTraceContext *trace_context)
{
  if (g_atomic_ref_count_dec (&trace_context->ref_count))
    {
      if (trace_context->writer)
        sysprof_capture_writer_flush (trace_context->writer);
      g_clear_pointer (&trace_context->writer, sysprof_capture_writer_unref);
      g_free (trace_context);
    }
}

static CoglTraceContext *
cogl_trace_context_ref (CoglTraceContext *trace_context)
{
  g_atomic_ref_count_inc (&trace_context->ref_count);
  return trace_context;
}

static gboolean
setup_trace_context (int          fd,
                     const char  *filename,
                     GError     **error)
{
  g_autoptr (GMutexLocker) locker = NULL;

  locker = g_mutex_locker_new (&cogl_trace_mutex);
  if (cogl_trace_context)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Trace context already setup");
      return FALSE;
    }

  cogl_trace_context = cogl_trace_context_new (fd, filename);

  if (!cogl_trace_context)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to setup trace context");
      return FALSE;
    }

  return TRUE;
}

static CoglTraceThreadContext *
cogl_trace_thread_context_new (const char       *group,
                               CoglTraceContext *trace_context)
{
  CoglTraceThreadContext *thread_context;
  pid_t tid;

  tid = (pid_t) syscall (SYS_gettid);

  thread_context = g_new0 (CoglTraceThreadContext, 1);
  thread_context->cpu_id = -1;
  thread_context->pid = getpid ();
  thread_context->group =
    group ? g_strdup (group) : g_strdup_printf ("t:%d", tid);
  thread_context->trace_context = cogl_trace_context_ref (trace_context);

  return thread_context;
}

static gboolean
enable_tracing_idle_callback (gpointer user_data)
{
  CoglTraceThreadContext *thread_context =
    g_private_get (&cogl_trace_thread_data);
  TraceData *data = user_data;

  if (thread_context)
    {
      g_warning ("Tracing already enabled");
      return G_SOURCE_REMOVE;
    }

  thread_context = cogl_trace_thread_context_new (data->group,
                                                  data->trace_context);
  g_private_set (&cogl_trace_thread_data, thread_context);

  return G_SOURCE_REMOVE;
}

static void
cogl_trace_thread_context_free (gpointer data)
{
  CoglTraceThreadContext *thread_context = data;

  if (!thread_context)
    return;

  g_free (thread_context->group);
  g_free (thread_context);
}

static gboolean
disable_tracing_idle_callback (gpointer user_data)
{
  CoglTraceThreadContext *thread_context =
    g_private_get (&cogl_trace_thread_data);

  if (!thread_context)
    {
      g_warning ("Tracing not enabled");
      return G_SOURCE_REMOVE;
    }

  g_private_replace (&cogl_trace_thread_data, NULL);

  return G_SOURCE_REMOVE;
}

gboolean
cogl_start_tracing_with_path (const char  *filename,
                              GError     **error)
{
  return setup_trace_context (-1, filename, error);
}

gboolean
cogl_start_tracing_with_fd (int      fd,
                            GError **error)
{
  return setup_trace_context (fd, NULL, error);
}

void
cogl_stop_tracing (void)
{
  g_mutex_lock (&cogl_trace_mutex);
  g_clear_pointer (&cogl_trace_context, cogl_trace_context_unref);
  g_mutex_unlock (&cogl_trace_mutex);
}

void
cogl_set_tracing_enabled_on_thread (GMainContext *main_context,
                                    const char   *group)
{
  TraceData *data;

  g_return_if_fail (cogl_trace_context);

  data = g_new0 (TraceData, 1);
  data->group = group ? strdup (group) : NULL;
  data->trace_context = cogl_trace_context_ref (cogl_trace_context);

  if (main_context == g_main_context_get_thread_default ())
    {
      enable_tracing_idle_callback (data);
      trace_data_free (data);
    }
  else
    {
      GSource *source;
      source = g_idle_source_new ();

      g_source_set_callback (source,
                             enable_tracing_idle_callback,
                             data,
                             trace_data_free);

      g_source_attach (source, main_context);
      g_source_unref (source);
    }
}

void
cogl_set_tracing_disabled_on_thread (GMainContext *main_context)
{
  if (g_main_context_get_thread_default () == main_context)
    {
      disable_tracing_idle_callback (NULL);
    }
  else
    {
      GSource *source;

      source = g_idle_source_new ();

      g_source_set_callback (source, disable_tracing_idle_callback, NULL, NULL);

      g_source_attach (source, main_context);
      g_source_unref (source);
    }
}

static void
cogl_trace_end_with_description (CoglTraceHead *head,
                                 const char    *description)
{
  SysprofTimeStamp end_time;
  CoglTraceContext *trace_context;
  CoglTraceThreadContext *trace_thread_context;

  end_time = g_get_monotonic_time () * 1000;
  trace_thread_context = g_private_get (&cogl_trace_thread_data);
  trace_context = trace_thread_context->trace_context;

  g_mutex_lock (&cogl_trace_mutex);
  if (!sysprof_capture_writer_add_mark (trace_context->writer,
                                        head->begin_time,
                                        trace_thread_context->cpu_id,
                                        trace_thread_context->pid,
                                        (uint64_t) end_time - head->begin_time,
                                        trace_thread_context->group,
                                        head->name,
                                        description))
    {
      /* XXX: g_main_context_get_thread_default() might be wrong, it probably
       * needs to store the GMainContext in CoglTraceThreadContext when creating
       * and use it here.
       */
      if (errno == EPIPE)
        cogl_set_tracing_disabled_on_thread (g_main_context_get_thread_default ());
    }
  g_mutex_unlock (&cogl_trace_mutex);
}

void
cogl_trace_end (CoglTraceHead *head)
{
  cogl_trace_end_with_description (head, head->description);
  g_free (head->description);
}

void
cogl_trace_mark (const char *name,
                 const char *description)
{
  SysprofTimeStamp time;
  CoglTraceContext *trace_context;
  CoglTraceThreadContext *trace_thread_context;

  time = g_get_monotonic_time () * 1000;
  trace_thread_context = g_private_get (&cogl_trace_thread_data);
  trace_context = trace_thread_context->trace_context;

  g_mutex_lock (&cogl_trace_mutex);
  if (!sysprof_capture_writer_add_mark (trace_context->writer,
                                        time,
                                        trace_thread_context->cpu_id,
                                        trace_thread_context->pid,
                                        0,
                                        trace_thread_context->group,
                                        name,
                                        description))
    {
      /* XXX: g_main_context_get_thread_default() might be wrong, it probably
       * needs to store the GMainContext in CoglTraceThreadContext when creating
       * and use it here.
       */
      if (errno == EPIPE)
        cogl_set_tracing_disabled_on_thread (g_main_context_get_thread_default ());
    }
  g_mutex_unlock (&cogl_trace_mutex);
}

void
cogl_trace_describe (CoglTraceHead *head,
                     const char    *description)
{
  if (head->description)
    {
      char *old_description = head->description;
      head->description =
        g_strdup_printf ("%s, %s", old_description, description);
      g_free (old_description);
    }
  else
    head->description = g_strdup (description);
}

#ifdef HAVE_TRACY

void
cogl_trace_tracy_begin (CoglTraceTracyHead *head, const CoglTraceTracyLocation *location)
{
  head->ctx = ___tracy_emit_zone_begin (location, TracyCIsStarted);
}

void
cogl_trace_tracy_end (CoglTraceTracyHead *head)
{
  ___tracy_emit_zone_end (head->ctx);
}

void
cogl_trace_tracy_describe (CoglTraceTracyHead *head, const char *description, size_t size)
{
  ___tracy_emit_zone_text (head->ctx, description, size);
}

void
cogl_trace_tracy_name_dynamic (CoglTraceTracyHead *head, const char *name, size_t size)
{
  ___tracy_emit_zone_name (head->ctx, name, size);
}

void
cogl_trace_tracy_emit_message (const char *message, size_t size)
{
  if (!TracyCIsStarted)
    return;

  ___tracy_emit_message (message, size, 0);
}

void
cogl_trace_tracy_emit_plot_double (const char *name, double val)
{
  if (!TracyCIsStarted)
    return;

  ___tracy_emit_plot (name, val);
}

uint8_t
cogl_trace_tracy_create_gpu_context (int64_t current_gpu_time_ns)
{
  static int context_id_counter = 1;
  int context_id;

  if (!TracyCIsStarted)
    return 0;

  context_id = g_atomic_int_add (&context_id_counter, 1);
  g_return_val_if_fail (context_id <= 255, 0);

  ___tracy_emit_gpu_new_context ((struct ___tracy_gpu_new_context_data) {
    .gpuTime = current_gpu_time_ns,
    .period = 1.,
    .context = context_id,
    .flags = 0,
    .type = 1, // OpenGL
  });

  return context_id;
}

void
cogl_trace_tracy_emit_gpu_time (uint8_t context_id, unsigned int query_id, int64_t gpu_time_ns)
{
  if (context_id == 0)
    return;

  ___tracy_emit_gpu_time ((struct ___tracy_gpu_time_data) {
    .gpuTime = gpu_time_ns,
    .queryId = query_id,
    .context = context_id,
  });
}

void
cogl_trace_tracy_emit_gpu_time_sync (uint8_t context_id, int64_t current_gpu_time_ns)
{
  if (context_id == 0)
    return;

  ___tracy_emit_gpu_time_sync ((struct ___tracy_gpu_time_sync_data) {
    .gpuTime = current_gpu_time_ns,
    .context = context_id,
  });
}

void
cogl_trace_tracy_begin_gpu_zone (uint8_t context_id, unsigned int query_id, const CoglTraceTracyLocation *location)
{
  if (context_id == 0)
    return;

  ___tracy_emit_gpu_zone_begin ((struct ___tracy_gpu_zone_begin_data) {
    .srcloc = (uint64_t) location,
    .queryId = query_id,
    .context = context_id,
  });
}

void
cogl_trace_tracy_end_gpu_zone (uint8_t context_id, unsigned int query_id)
{
  if (context_id == 0)
    return;

  ___tracy_emit_gpu_zone_end ((struct ___tracy_gpu_zone_end_data) {
    .queryId = query_id,
    .context = context_id,
  });
}

#endif /* HAVE_TRACY */

#else

#include <string.h>
#include <stdio.h>

gboolean
cogl_start_tracing_with_path (const char  *filename,
                              GError     **error)
{
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Tracing disabled at build time");
  return FALSE;
}

gboolean
cogl_start_tracing_with_fd (int      fd,
                            GError **error)
{
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Tracing disabled at build time");
  return FALSE;
}

void
cogl_stop_tracing (void)
{
  fprintf (stderr, "Tracing not enabled");
}

void
cogl_set_tracing_enabled_on_thread (void       *data,
                                    const char *group)
{
  fprintf (stderr, "Tracing not enabled");
}

void
cogl_set_tracing_disabled_on_thread (void *data)
{
  fprintf (stderr, "Tracing not enabled");
}

#endif /* HAVE_PROFILER */

void
cogl_trace_tracy_start (void)
{
#ifdef TRACY_MANUAL_LIFETIME
  if (!TracyCIsStarted)
    ___tracy_startup_profiler ();
#endif
}

gboolean
cogl_trace_tracy_is_active (void)
{
#ifdef HAVE_TRACY
  return TracyCIsStarted && TracyCIsConnected;
#else
  return FALSE;
#endif
}
