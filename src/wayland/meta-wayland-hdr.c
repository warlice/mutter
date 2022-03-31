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

#include "config.h"

#include <drm_fourcc.h>
#include "backends/meta-backend-private.h"
#include "meta/meta-backend.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-hdr.h"

#include "hdr-metadata-unstable-v1-server-protocol.h"

#define STATIC_METADATA(x) data->metadata.static_metadata.x

/* Implements the protocol function set metadata */
static void
hdr_surface_set_metadata (struct wl_client *client,
                          struct wl_resource *surface_resource,
			  uint32_t primary_r_x, uint32_t primary_r_y,
                          uint32_t primary_g_x, uint32_t primary_g_y,
                          uint32_t primary_b_x, uint32_t primary_b_y,
                          uint32_t white_point_x, uint32_t white_point_y,
                          uint32_t max_luminance, uint32_t min_luminance,
                          uint32_t max_cll, uint32_t max_fall)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  meta_verbose ("Implement set hdr metadata on the surface");

  if (!surface->pending_state->hdr_metadata) {
    meta_verbose ("Implement set hdr metadata on the surface, allocate memory \n");
    surface->pending_state->hdr_metadata = g_malloc0 (sizeof(MetaOutputHdrMetadata));
  }

  MetaOutputHdrMetadata *metadata = surface->pending_state->hdr_metadata;

  metadata->mastering_display_primaries[0].x = wl_fixed_to_double(primary_r_x);
  metadata->mastering_display_primaries[0].y = wl_fixed_to_double(primary_r_y);
  metadata->mastering_display_primaries[1].x = wl_fixed_to_double(primary_g_x);
  metadata->mastering_display_primaries[1].y = wl_fixed_to_double(primary_g_y);
  metadata->mastering_display_primaries[2].x = wl_fixed_to_double(primary_b_x);
  metadata->mastering_display_primaries[2].y = wl_fixed_to_double(primary_b_y);
  metadata->mastering_display_white_point.x  = wl_fixed_to_double(white_point_x);
  metadata->mastering_display_white_point.y  = wl_fixed_to_double(white_point_y);
  metadata->mastering_display_max_luminance  = wl_fixed_to_double(max_luminance);
  metadata->mastering_display_min_luminance  = wl_fixed_to_double(min_luminance);
  metadata->max_cll  = max_cll;
  metadata->max_fall  = max_fall;

}

/* Implements the protocol function set_eotf */
static void
hdr_surface_set_eotf (struct wl_client *client,
                      struct wl_resource *surface_resource,
		      uint32_t eotf)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  meta_verbose ("Implement set hdr transfer function (eotf) on the surface");

  MetaOutputHdrMetadata *metadata = surface->pending_state->hdr_metadata;
  MetaOutputHdrMetadataEOTF internal_eotf = META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR;

  switch (eotf) {
    case ZWP_HDR_SURFACE_V1_EOTF_ST_2084_PQ:
      internal_eotf = META_OUTPUT_HDR_METADATA_EOTF_PQ;
      break;
    case ZWP_HDR_SURFACE_V1_EOTF_HLG:
      internal_eotf = META_OUTPUT_HDR_METADATA_EOTF_HLG;
      break;
  }

  metadata->eotf = internal_eotf;

}

/* Destroy the hdr surface */
static void
hdr_surface_destroy (struct wl_client *client,
                     struct wl_resource *resource)
{
  meta_verbose ("hdr surface Destroy \n");
}

/* Destroy the zwp_hdr_surface_v1_interface Wayland object */
static void
destroy_hdr_surface (struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  meta_verbose ("Destroy the hdr surface ==========> \n");
}

static const struct zwp_hdr_surface_v1_interface
hdr_surface_implementation = {
  hdr_surface_set_metadata,
  hdr_surface_set_eotf,
  hdr_surface_destroy
};

/* Implements the zwp_hdr_surface_v1_interface Wayland object */
static void
hdr_metadata_get_hdr_surface (struct wl_client   *client,
                              struct wl_resource *hdr_metadata,
                              uint32_t id,
                              struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  struct wl_resource *resource;

  meta_verbose ("Implement get hdr surface \n");

  if (surface->hdr_surface_resource) {
    wl_resource_post_error(hdr_metadata,
                           ZWP_HDR_METADATA_V1_ERROR_HDR_SURFACE_EXISTS,
                           "a hdr surface for that surface already exists");
    return;
  }

  resource = wl_resource_create (client,
                                 &zwp_hdr_surface_v1_interface,
                                 wl_resource_get_version (hdr_metadata), id);

  if (resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation (resource,
                                  &hdr_surface_implementation,
                                  surface, destroy_hdr_surface);

  surface->hdr_surface_resource = resource;
  surface->pending_state->hdr_metadata =
          g_malloc0 (sizeof(MetaOutputHdrMetadata));

  if (!surface->pending_state->hdr_metadata) {
    wl_client_post_no_memory(client);
    return;
  }

  return;
}

/* Destroy the zwp_hdr_metadata_v1_interface Wayland object */
static void
hdr_metadata_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  meta_verbose ("hdr metadata Destroy \n");

  wl_resource_destroy (resource);
}

static const struct zwp_hdr_metadata_v1_interface
hdr_metadata_implementation =
{
  hdr_metadata_destroy,
  hdr_metadata_get_hdr_surface
};

/* Implements the zwp_hdr_metadata_v1_interface Wayland object */
static void
hdr_metadata_bind (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  MetaWaylandCompositor *compositor = data;
  struct wl_resource *resource;

  meta_verbose ("hdr metadata bind \n");

  resource = wl_resource_create (client, &zwp_hdr_metadata_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &hdr_metadata_implementation,
                                  compositor, NULL);
}

/**
 * meta_wayland_hdr_metadata_init:
 * @compositor: The #MetaWaylandCompositor
 *
 * Creates the global Wayland object that exposes the hdr-metadata protocol.
 *
 * Returns: Whether the initialization was succesfull. If this is %FALSE,
 * clients won't be able to use the hdr-metadata protocol.
 */
gboolean
meta_wayland_hdr_init (MetaWaylandCompositor *compositor)
{
  if (!wl_global_create (compositor->wayland_display,
                         &zwp_hdr_metadata_v1_interface,
                         META_ZWP_HDR_METADATA_V1_VERSION,
                         compositor,
                         hdr_metadata_bind))
    return FALSE;

  return TRUE;
}

