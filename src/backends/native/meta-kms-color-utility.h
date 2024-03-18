/*
 * Copyright (C) 2024 Intel Corporation.
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
 *   Sameer Lattannavar <sameer.lattannavar@intel.com>
 *   Naveen Kumar <naveen1.kumar@intel.com>
 *
 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "backends/native/meta-kms-crtc.h"

typedef struct _MetaChromaticity
{
  double x;
  double y;
  double luminance;
} MetaChromaticity;

typedef struct _MetaColorSpace
{
  MetaChromaticity white;
  MetaChromaticity red;
  MetaChromaticity green;
  MetaChromaticity blue;
} MetaColorSpace;

typedef struct _MetaOneDLUT
{
  int nSamples;
  double maxVal;
  double *pLutData;
} MetaOneDLUT;

//Exposed APIs for compositor/backend to use them for color caclulations:
extern void meta_color_utility_get_unity_log_lut (uint32_t num_of_segments,
                                                  uint32_t *num_of_entries_per_segment,
                                                  MetaGammaLut *gamma);
extern void meta_color_utility_get_transformation_matrix (MetaColorSpace srccs,
                                                          MetaColorSpace destcs,
                                                          MetaCtm *ctm);
extern void meta_color_utility_generate_srgb_degamma_lut (MetaDegammaLut *degamma);
extern void meta_color_utility_generate_srgb_gamma_lut (MetaGammaLut *gamma);

//Supporting APIs to exposed APIs:
double meta_color_utility_matrix_determinant_3x3 (double matrix[3][3]);
int meta_color_utility_matrix_inverse_3x3 (double matrix[3][3],
                                           double result[3][3]);
void meta_color_utility_matrix_multi_3x3_with_3x1 (double matrix1[3][3],
                                                  double matrix2[3],
                                                  double result[3]);
void meta_color_utility_matrix_multi_3x3 (double matrix1[3][3],
                                          double matrix2[3][3],
                                          double result[3][3]);
void meta_color_utility_create_rgb2xyz_matrix (MetaColorSpace *pcspace,
                                               double rgb2xyz[3][3]);
void meta_color_utility_create_gamut_scaling_matrix (MetaColorSpace *psrc,
                                                     MetaColorSpace *pdst,
                                                     double result[3][3]);
double meta_color_utility_oetf_2084 (double input,
                                     double srcmaxluminance);
double meta_color_utility_eotf_2084 (double input);
void meta_color_utility_generate_oetf_2084_lut (MetaOneDLUT *lut);
void meta_color_utility_generate_eotf_2084_lut (MetaOneDLUT *lut);
void meta_color_utility_populate_standard_lut (char *standardlutkey,
                                               MetaOneDLUT *lut);
double meta_color_utility_get_srgb_decoding_value (double input);
double meta_color_utility_get_srgb_encoding_value (double input);

