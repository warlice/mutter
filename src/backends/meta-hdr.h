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
 *
 */

#ifndef META_HDR_H
#define META_HDR_H

#include <glib.h>
#include <glib-object.h>
#include <stdint.h>
#include <meta/types.h>

#include "backends/meta-output.h"

#define META_TYPE_HDR (meta_hdr_get_type ())

G_DECLARE_FINAL_TYPE (MetaHdr, meta_hdr,
                      META, HDR, GObject)

MetaHdr * meta_hdr_new (MetaBackend *backend);

gboolean meta_hdr_maybe_supports_metadata (MetaBackend *backend);

void meta_hdr_set_metadata (MetaBackend *backend, MetaOutputHdrMetadata *metadata);

void meta_hdr_set_eotf (MetaBackend *backend, MetaOutputHdrMetadataEOTF eotf);

void meta_hdr_set_colorspace (MetaBackend *backend, MetaOutputColorspace colorspace);

MetaOutputHdrMetadata * meta_hdr_get_metadata (MetaBackend *backend);

void meta_hdr_get_colorspaces (MetaOutputColorspace *client_colorspace,
                               MetaOutputColorspace *target_colorspace);

void meta_hdr_destroy_metadata (MetaBackend *backend);

#endif /* META_HDR_H */

