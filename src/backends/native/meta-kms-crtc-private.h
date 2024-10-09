/*
 * Copyright (C) 2019 Red Hat
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

#pragma once

#include <xf86drmMode.h>

#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-update-private.h"

typedef enum _MetaKmsCrtcProp
{
  META_KMS_CRTC_PROP_MODE_ID = 0,
  META_KMS_CRTC_PROP_ACTIVE,
  META_KMS_CRTC_PROP_GAMMA_LUT,
  META_KMS_CRTC_PROP_GAMMA_LUT_SIZE,
  META_KMS_CRTC_PROP_VRR_ENABLED,
  META_KMS_CRTC_PROP_GLOBAL_HISTOGRAM_ENABLED,
  META_KMS_CRTC_PROP_GLOBAL_HISTOGRAM,
  META_KMS_CRTC_N_PROPS
} MetaKmsCrtcProp;

typedef enum _MetaKmsCrtcHistogram
{
  META_KMS_CRTC_HISTOGRAM_DISABLE = 0,
  META_KMS_CRTC_HISTOGRAM_ENABLE,
  META_KMS_CRTC_HISTOGRAM_N_PROPS,
  META_KMS_CRTC_HISTOGRAM_UNKNOWN,
} MetaKmsCrtcHistogram;

MetaKmsCrtc * meta_kms_crtc_new (MetaKmsImplDevice  *impl_device,
                                 drmModeCrtc        *drm_crtc,
                                 int                 idx,
                                 GError            **error);

MetaKmsResourceChanges meta_kms_crtc_update_state_in_impl (MetaKmsCrtc *crtc,
                                                           gboolean     read_histogram);

void meta_kms_crtc_disable_in_impl (MetaKmsCrtc *crtc);

void meta_kms_crtc_predict_state_in_impl (MetaKmsCrtc   *crtc,
                                  MetaKmsUpdate *update);

uint32_t meta_kms_crtc_get_prop_id (MetaKmsCrtc     *crtc,
                                    MetaKmsCrtcProp  prop);

const char * meta_kms_crtc_get_prop_name (MetaKmsCrtc     *crtc,
                                          MetaKmsCrtcProp  prop);

uint64_t meta_kms_crtc_get_prop_drm_value (MetaKmsCrtc     *crtc,
                                           MetaKmsCrtcProp  prop,
                                           uint64_t         value);

gboolean meta_kms_crtc_determine_deadline (MetaKmsCrtc  *crtc,
                                           int64_t      *out_next_deadline_us,
                                           int64_t      *out_next_presentation_us,
                                           GError      **error);

void meta_kms_crtc_set_is_leased (MetaKmsCrtc *crtc,
                                  gboolean     leased);
