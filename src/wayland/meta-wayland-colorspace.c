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
#include "wayland/meta-wayland-colorspace.h"

#include "colorspace-unstable-v1-server-protocol.h"

/* Implements the protocol function set_colorspace */
static void
colorspace_set (struct wl_client *client,
                struct wl_resource *resource,
                struct wl_resource *surface_resource,
                uint32_t chromacities)
{
  static uint32_t colorspace_names[] = {
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_UNKNOWN] = META_OUTPUT_COLORSPACE_UNKNOWN,
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_BT601_525_LINE] = META_OUTPUT_COLORSPACE_BT601_525_LINE,
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_BT601_625_LINE] = META_OUTPUT_COLORSPACE_BT601_625_LINE,
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_SMPTE170M] = META_OUTPUT_COLORSPACE_SMPTE170M,
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_BT709] = META_OUTPUT_COLORSPACE_BT709,
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_BT2020] = META_OUTPUT_COLORSPACE_BT2020,
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_SRGB] = META_OUTPUT_COLORSPACE_DEFAULT,
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_DISPLAYP3] = META_OUTPUT_COLORSPACE_DISPLAYP3,
    [ZWP_COLORSPACE_V1_CHROMATICITY_NAMES_ADOBERGB] = META_OUTPUT_COLORSPACE_ADOBERGB,
  };

  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  meta_verbose ("set colorspace \n");
  surface->pending_state->colorspace = colorspace_names[chromacities];
}

/* Destroy the zwp_colorspace_v1_interface Wayland object */
static void
colorspace_destroy (struct wl_client   *client,
                    struct wl_resource *resource)
{
  meta_verbose ("colorspace Destroy \n");

  wl_resource_destroy (resource);
}

static const struct zwp_colorspace_v1_interface
colorspace_implementation =
{
  colorspace_destroy,
  colorspace_set
};

/* Implements the zwp_colorspace_v1_interface Wayland object */
static void
colorspace_bind (struct wl_client *client,
                 void             *data,
                 uint32_t          version,
                 uint32_t          id)
{
  MetaWaylandCompositor *compositor = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &zwp_colorspace_v1_interface,
                                 version, id);

  if (resource == NULL) {
    wl_client_post_no_memory (client);
    return;
  }

  meta_verbose ("colorspace bind \n");
  wl_resource_set_implementation (resource, &colorspace_implementation,
                                  compositor, NULL);
}

/**
 * meta_wayland_colorspace_init:
 * @compositor: The #MetaWaylandCompositor
 *
 * Creates the global Wayland object that exposes the colorspace protocol.
 *
 * Returns: Whether the initialization was succesfull. If this is %FALSE,
 * clients won't be able to use the colorspace protocol for color space conversion.
 */
gboolean
meta_wayland_colorspace_init (MetaWaylandCompositor *compositor)
{
  if (!wl_global_create (compositor->wayland_display,
                         &zwp_colorspace_v1_interface,
                         META_ZWP_COLORSPACE_V1_VERSION,
                         compositor,
                         colorspace_bind))
    return FALSE;

  return TRUE;
}
