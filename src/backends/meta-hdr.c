/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2023 Intel Corporation.
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
 * along with this program;
 *
 * Authors:
 *   Naveen Kumar <naveen1.kumar@intel.com>
 */

#include "config.h"

#include "backends/meta-hdr.h"
#include "meta/util.h"

typedef struct _MetaHdr
{
  GObject parent;
  MetaBackend *backend;
  gboolean has_metadata;
  MetaOutputHdrMetadata *metadata;
  MetaOutputHdrMetadataEOTF eotf;
  MetaOutputColorspace colorspace;
} MetaHdr;

G_DEFINE_TYPE (MetaHdr, meta_hdr, G_TYPE_OBJECT);

MetaHdr *
meta_hdr_new (MetaBackend *backend)
{
  MetaHdr *hdr;

  hdr = g_object_new (META_TYPE_HDR, NULL);
  hdr->backend = backend;

  return hdr;
}

gboolean
meta_hdr_maybe_supports_metadata (MetaBackend *backend)
{
  MetaHdr *hdr =
         meta_backend_get_hdr (backend);

  return hdr->has_metadata;
}

void
meta_hdr_get_colorspaces (MetaOutputColorspace *client_colorspace,
                          MetaOutputColorspace *target_colorspace)
{
  // TODO: ignore colorspace for now
}

MetaOutputHdrMetadata *
meta_hdr_get_metadata (MetaBackend *backend)
{

  MetaHdr *hdr =
         meta_backend_get_hdr (backend);

  return hdr->metadata;
}

void
meta_hdr_set_metadata (MetaBackend *backend, MetaOutputHdrMetadata *metadata)
{
  MetaHdr *hdr =
         meta_backend_get_hdr (backend);

  if (metadata == NULL)
    return;

  hdr->has_metadata = TRUE;
  hdr->metadata = metadata;
}

void
meta_hdr_set_eotf (MetaBackend *backend, MetaOutputHdrMetadataEOTF eotf)
{
  MetaHdr *hdr =
         meta_backend_get_hdr (backend);

  hdr->eotf = eotf;
}

void
meta_hdr_set_colorspace (MetaBackend *backend, MetaOutputColorspace colorspace)
{
  MetaHdr *hdr =
         meta_backend_get_hdr (backend);

  hdr->colorspace = colorspace;
}

void
meta_hdr_destroy_metadata (MetaBackend *backend)
{
  MetaHdr *hdr =
         meta_backend_get_hdr (backend);

  hdr->has_metadata = FALSE;
  hdr->metadata = NULL;
  hdr->eotf = 0;
}

static void
meta_hdr_finalize (GObject *object)
{
  G_OBJECT_CLASS (meta_hdr_parent_class)->finalize (object);
}

static void
meta_hdr_init (MetaHdr *hdr)
{
}

static void
meta_hdr_class_init (MetaHdrClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_hdr_finalize;
}
