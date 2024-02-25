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

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>
#include <errno.h>

#include "cogl/cogl-macros.h"

#ifdef HAVE_PROFILER

typedef struct _CoglTraceContext CoglTraceContext;

typedef struct _CoglTraceHead
{
  uint64_t begin_time;
  const char *name;
  char *description;
} CoglTraceHead;

COGL_EXPORT
GPrivate cogl_trace_thread_data;
COGL_EXPORT
CoglTraceContext *cogl_trace_context;
COGL_EXPORT
GMutex cogl_trace_mutex;

COGL_EXPORT
gboolean cogl_start_tracing_with_path (const char  *filename,
                                       GError     **error);

COGL_EXPORT
gboolean cogl_start_tracing_with_fd (int      fd,
                                     GError **error);

COGL_EXPORT
void cogl_stop_tracing (void);

COGL_EXPORT
void cogl_set_tracing_enabled_on_thread (GMainContext *main_context,
                                         const char   *group);

COGL_EXPORT
void cogl_set_tracing_disabled_on_thread (GMainContext *main_context);

static inline void
cogl_trace_begin (CoglTraceHead *head,
                  const char    *name)
{
  head->begin_time = g_get_monotonic_time () * 1000;
  head->name = name;
}

COGL_EXPORT void
cogl_trace_end (CoglTraceHead *head);

COGL_EXPORT void
cogl_trace_describe (CoglTraceHead *head,
                     const char    *description);

COGL_EXPORT void
cogl_trace_mark (const char *name,
                 const char *description);

static inline void
cogl_auto_trace_end_helper (CoglTraceHead **head)
{
  if (*head)
    cogl_trace_end (*head);
}

static inline gboolean
cogl_is_tracing_enabled (void)
{
  return !!g_private_get (&cogl_trace_thread_data);
}

#ifdef HAVE_TRACY

#ifdef COGL_GIR_SCANNING
typedef void CoglTraceTracyLocation;
typedef void CoglTraceTracyHead;
#else

// Tracy structs that we want stack-allocated are inlined here
// to avoid adding a public Tracy include dependency.
#ifndef __TRACYC_HPP__

struct ___tracy_source_location_data
{
    const char* name;
    const char* function;
    const char* file;
    uint32_t line;
    uint32_t color;
};

struct ___tracy_c_zone_context
{
    uint32_t id;
    int active;
};

#endif /* __TRACYC_HPP__ */

typedef struct ___tracy_source_location_data CoglTraceTracyLocation;

typedef struct _CoglTraceTracyHead
{
  struct ___tracy_c_zone_context ctx;
} CoglTraceTracyHead;

#define COGL_TRACE_TRACY_LOCATION_INIT(_name) \
  (CoglTraceTracyLocation) { \
    .name = _name, \
    .function = __func__, \
    .file = __FILE__, \
    .line = __LINE__, \
    .color = 0, \
  }

#endif /* COGL_GIR_SCANNING */

COGL_EXPORT void
cogl_trace_tracy_begin (CoglTraceTracyHead *head, const CoglTraceTracyLocation *location);

COGL_EXPORT void
cogl_trace_tracy_end (CoglTraceTracyHead *head);

COGL_EXPORT void
cogl_trace_tracy_describe (CoglTraceTracyHead *head, const char *description, size_t size);

COGL_EXPORT void
cogl_trace_tracy_name_dynamic (CoglTraceTracyHead *head, const char *name, size_t size);

COGL_EXPORT void
cogl_trace_tracy_emit_message (const char *message, size_t size);

COGL_EXPORT void
cogl_trace_tracy_emit_plot_double (const char *name, double val);

COGL_EXPORT void
cogl_trace_tracy_emit_frame_mark_start (const char *name);

COGL_EXPORT void
cogl_trace_tracy_emit_frame_mark_end (const char *name);

COGL_EXPORT uint8_t
cogl_trace_tracy_create_gpu_context (int64_t current_gpu_time_ns);

COGL_EXPORT void
cogl_trace_tracy_emit_gpu_time (uint8_t context_id, unsigned int query_id, int64_t gpu_time_ns);

COGL_EXPORT void
cogl_trace_tracy_emit_gpu_time_sync (uint8_t context_id, int64_t current_gpu_time_ns);

COGL_EXPORT void
cogl_trace_tracy_begin_gpu_zone (uint8_t context_id, unsigned int query_id, const CoglTraceTracyLocation *location);

COGL_EXPORT void
cogl_trace_tracy_end_gpu_zone (uint8_t context_id, unsigned int query_id);

static inline void
cogl_auto_trace_end_helper_tracy (CoglTraceTracyHead *head)
{
  cogl_trace_tracy_end (head);
}

#define COGL_TRACE_TRACY_BEGIN_SCOPED(Name, name) \
  static const CoglTraceTracyLocation \
    CoglTraceTracyLocation##Name = COGL_TRACE_TRACY_LOCATION_INIT (name); \
  __attribute__((cleanup (cogl_auto_trace_end_helper_tracy))) \
    CoglTraceTracyHead CoglTraceTracy##Name = { 0 }; \
  cogl_trace_tracy_begin (&CoglTraceTracy##Name, &CoglTraceTracyLocation##Name);

#define COGL_TRACE_TRACY_END(Name) \
  cogl_trace_tracy_end (&CoglTraceTracy##Name); \
  CoglTraceTracy##Name.ctx.active = 0;

#define COGL_TRACE_TRACY_DESCRIBE(Name, description)\
  if (CoglTraceTracy##Name.ctx.active && description) \
    cogl_trace_tracy_describe (&CoglTraceTracy##Name, description, strlen (description));

#define COGL_TRACE_TRACY_SCOPED_ANCHOR(Name) \
  __attribute__((cleanup (cogl_auto_trace_end_helper_tracy))) \
    CoglTraceTracyHead CoglTraceTracy##Name = { 0 };

#define COGL_TRACE_TRACY_BEGIN_ANCHORED(Name, name) \
  static const CoglTraceTracyLocation \
    CoglTraceTracyLocation##Name = COGL_TRACE_TRACY_LOCATION_INIT (name); \
  cogl_trace_tracy_begin (&CoglTraceTracy##Name, &CoglTraceTracyLocation##Name);

#define COGL_TRACE_TRACY_MESSAGE(message) \
  cogl_trace_tracy_emit_message (message, strlen (message));

#define COGL_TRACE_PLOT_DOUBLE(name, val) \
  cogl_trace_tracy_emit_plot_double (name, val)

#define COGL_TRACE_NAME_DYNAMIC(Name, ...)\
  G_STMT_START \
    { \
      g_autofree char *_name = g_strdup_printf (__VA_ARGS__); \
      cogl_trace_tracy_name_dynamic (&CoglTraceTracy##Name, _name, strlen (_name)); \
    } \
  G_STMT_END

#define COGL_TRACE_FRAME_START(name) \
  cogl_trace_tracy_emit_frame_mark_start (name)

#define COGL_TRACE_FRAME_END(name) \
  cogl_trace_tracy_emit_frame_mark_end (name)

#else /* HAVE_TRACY */

#define COGL_TRACE_TRACY_BEGIN_SCOPED(Name, name) (void) 0
#define COGL_TRACE_TRACY_END(Name) (void) 0
#define COGL_TRACE_TRACY_DESCRIBE(Name, description) (void) 0
#define COGL_TRACE_TRACY_SCOPED_ANCHOR(Name) (void) 0
#define COGL_TRACE_TRACY_BEGIN_ANCHORED(Name, name) (void) 0
#define COGL_TRACE_TRACY_MESSAGE(message) (void) 0

#define COGL_TRACE_PLOT_DOUBLE(name, val) (void) 0
#define COGL_TRACE_NAME_DYNAMIC(Name, ...) (void) 0
#define COGL_TRACE_FRAME_START(name) (void) 0
#define COGL_TRACE_FRAME_END(name) (void) 0

#endif /* HAVE_TRACY */

#define COGL_TRACE_BEGIN_SCOPED(Name, name) \
  COGL_TRACE_TRACY_BEGIN_SCOPED (Name, name); \
  CoglTraceHead CoglTrace##Name = { 0 }; \
  __attribute__((cleanup (cogl_auto_trace_end_helper))) \
    CoglTraceHead *ScopedCoglTrace##Name = NULL; \
  if (cogl_is_tracing_enabled ()) \
    { \
      cogl_trace_begin (&CoglTrace##Name, name); \
      ScopedCoglTrace##Name = &CoglTrace##Name; \
    }

#define COGL_TRACE_END(Name)\
  if (cogl_is_tracing_enabled ()) \
    { \
      cogl_trace_end (&CoglTrace##Name); \
      ScopedCoglTrace##Name = NULL; \
    } \
  COGL_TRACE_TRACY_END (Name);

#define COGL_TRACE_DESCRIBE(Name, description)\
  const char *CoglTrace##Name##Description = (description); \
  COGL_TRACE_TRACY_DESCRIBE (Name, CoglTrace##Name##Description); \
  if (cogl_is_tracing_enabled ()) \
    cogl_trace_describe (&CoglTrace##Name, CoglTrace##Name##Description);

#define COGL_TRACE_SCOPED_ANCHOR(Name) \
  COGL_TRACE_TRACY_SCOPED_ANCHOR (Name); \
  CoglTraceHead G_GNUC_UNUSED CoglTrace##Name = { 0 }; \
  __attribute__((cleanup (cogl_auto_trace_end_helper))) \
    CoglTraceHead *ScopedCoglTrace##Name = NULL; \

#define COGL_TRACE_BEGIN_ANCHORED(Name, name) \
  COGL_TRACE_TRACY_BEGIN_ANCHORED (Name, name); \
  if (cogl_is_tracing_enabled ()) \
    { \
      cogl_trace_begin (&CoglTrace##Name, name); \
      ScopedCoglTrace##Name = &CoglTrace##Name; \
    }

#define COGL_TRACE_MESSAGE(name, ...) \
  G_STMT_START \
    { \
      if (cogl_is_tracing_enabled () || cogl_trace_tracy_is_active ()) \
        { \
          g_autofree char *CoglTraceMessage = g_strdup_printf (__VA_ARGS__); \
          COGL_TRACE_TRACY_MESSAGE (CoglTraceMessage); \
          if (cogl_is_tracing_enabled ()) \
            cogl_trace_mark (name, CoglTraceMessage); \
        } \
    } \
  G_STMT_END

#else /* HAVE_PROFILER */

#include <stdio.h>

#define COGL_TRACE_BEGIN_SCOPED(Name, name) (void) 0
#define COGL_TRACE_END(Name) (void) 0
#define COGL_TRACE_DESCRIBE(Name, description) (void) 0
#define COGL_TRACE_SCOPED_ANCHOR(Name) (void) 0
#define COGL_TRACE_BEGIN_ANCHORED(Name, name) (void) 0
#define COGL_TRACE_MESSAGE(name, ...) (void) 0
#define COGL_TRACE_PLOT_DOUBLE(name, val) (void) 0
#define COGL_TRACE_NAME_DYNAMIC(Name, ...) (void) 0

COGL_EXPORT
gboolean cogl_start_tracing_with_path (const char  *filename,
                                       GError     **error);

COGL_EXPORT
gboolean cogl_start_tracing_with_fd (int      fd,
                                     GError **error);

COGL_EXPORT
void cogl_stop_tracing (void);

COGL_EXPORT
void cogl_set_tracing_enabled_on_thread (void       *data,
                                         const char *group);

COGL_EXPORT
void cogl_set_tracing_disabled_on_thread (void *data);

#endif /* HAVE_PROFILER */

COGL_EXPORT void
cogl_trace_tracy_start (void);

COGL_EXPORT gboolean
cogl_trace_tracy_is_active (void);
