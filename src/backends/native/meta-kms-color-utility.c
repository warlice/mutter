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

/*
 * MetaColorUtility : Class works as an interface to perform the Color Specific
 * computations to  generate LUT values, CTM for various Color Spaces.
 *
 * If the Display KMS does not support the selected target colorspace & Gamma
 * mode, then the conversions & computations  will happen through GL Shaders
 */

#include <glib-object.h>
#include "backends/native/meta-kms-color-utility.h"
#include "backends/native/meta-kms.h"

//Reference: https://nick-shaw.github.io/cinematiccolor/common-rgb-color-spaces.html
double m1 = 0.1593017578125;
double m2 = 78.84375;
double c1 = 0.8359375;
double c2 = 18.8515625;
double c3 = 18.6875;

#define DD_MIN(a, b) ((a) < (b) ? (a) : (b))
#define DD_MAX(a, b) ((a) < (b) ? (b) : (a))
#define MAX_24BIT_NUM ((1<<24) -1)

void meta_color_utility_get_unity_log_lut (uint32_t num_of_segments,
                                           uint32_t *num_of_entries_per_segment,
                                           MetaGammaLut *gamma)
{
  uint32_t temp, index = 0;
  double scaling_factor;
  uint32_t max_hw_value = (1 << 16) - 1;
  unsigned int max_segment_value = 1 << 24;

  gamma->green[index] = gamma->blue[index] = gamma->red[index] = 0;
  for (int segment=0; segment < num_of_segments; segment++)
    {
      uint32_t entry_count = num_of_entries_per_segment[segment];
      uint32_t start = (1 << (segment - 1));
      uint32_t end = (1 << segment);
      for (uint32_t entry = 1; entry <= entry_count; entry++)
        {
          index++;
          scaling_factor = (double)max_hw_value / (double)max_segment_value;
          temp = start + entry * ((end - start) * 1.0 / entry_count);

          gamma->red[index] = (double)temp * (double)scaling_factor;
          gamma->red[index] = DD_MIN(gamma->red[index], max_hw_value);
          gamma->green[index] = gamma->blue[index] = gamma->red[index];
        }
    }
}

double meta_color_utility_matrix_determinant_3x3 (double matrix[3][3])
{
  double result;
  result = matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]);
  result -= matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]);
  result += matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);

  return result;
}

int meta_color_utility_matrix_inverse_3x3 (double matrix[3][3],
                                           double result[3][3])
{
  int retVal = -1;
  double tmp[3][3];
  double determinant = meta_color_utility_matrix_determinant_3x3 (matrix);
  if (determinant)
    {
	tmp[0][0] = (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) / determinant;
	tmp[0][1] = (matrix[0][2] * matrix[2][1] - matrix[2][2] * matrix[0][1]) / determinant;
	tmp[0][2] = (matrix[0][1] * matrix[1][2] - matrix[0][2] * matrix[1][1]) / determinant;
	tmp[1][0] = (matrix[1][2] * matrix[2][0] - matrix[1][0] * matrix[2][2]) / determinant;
	tmp[1][1] = (matrix[0][0] * matrix[2][2] - matrix[0][2] * matrix[2][0]) / determinant;
	tmp[1][2] = (matrix[0][2] * matrix[1][0] - matrix[0][0] * matrix[1][2]) / determinant;
	tmp[2][0] = (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]) / determinant;
	tmp[2][1] = (matrix[0][1] * matrix[2][0] - matrix[0][0] * matrix[2][1]) / determinant;
	tmp[2][2] = (matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0]) / determinant;

	result[0][0] = tmp[0][0];
	result[0][1] = tmp[0][1];
	result[0][2] = tmp[0][2];
	result[1][0] = tmp[1][0];
	result[1][1] = tmp[1][1];
	result[1][2] = tmp[1][2];
	result[2][0] = tmp[2][0];
	result[2][1] = tmp[2][1];
	result[2][2] = tmp[2][2];
	retVal = 0;
    }
  return retVal;
}

void meta_color_utility_matrix_multi_3x3_with_3x1 (double matrix1[3][3],
                                                   double matrix2[3],
                                                   double result[3])
{
  double tmp[3];
  tmp[0] = matrix1[0][0] * matrix2[0] + matrix1[0][1] * matrix2[1] + matrix1[0][2] * matrix2[2];
  tmp[1] = matrix1[1][0] * matrix2[0] + matrix1[1][1] * matrix2[1] + matrix1[1][2] * matrix2[2];
  tmp[2] = matrix1[2][0] * matrix2[0] + matrix1[2][1] * matrix2[1] + matrix1[2][2] * matrix2[2];

  result[0] = tmp[0];
  result[1] = tmp[1];
  result[2] = tmp[2];
}

void meta_color_utility_matrix_multi_3x3 (double matrix1[3][3],
                                          double matrix2[3][3],
                                          double result[3][3])
{
  double tmp[3][3];
  for (uint8_t y = 0; y < 3; y++)
    {
      for (uint8_t x = 0; x < 3; x++)
        {
          tmp[y][x] = matrix1[y][0] * matrix2[0][x] + matrix1[y][1] * matrix2[1][x] + matrix1[y][2] * matrix2[2][x];
        }
    }
  for (uint8_t y = 0; y < 3; y++)
    {
      for (uint8_t x = 0; x < 3; x++)
        {
          result[y][x] = tmp[y][x];
        }
    }
}

void meta_color_utility_create_rgb2xyz_matrix (MetaColorSpace *pcspace,
                                               double rgb2xyz[3][3])
{
  /*
     http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
  */
  double XYZsum[3];
  double z[4];
  double XYZw[3];
  MetaChromaticity *pChroma = &pcspace->white;
  for (uint8_t i = 0; i < 4; i++)
    {
      z[i] = 1 - pChroma[i].x - pChroma[i].y;
    }
  XYZw[0] = pcspace->white.x / pcspace->white.y;
  XYZw[1] = 1;
  XYZw[2] = z[0] / pcspace->white.y;

  double xyzrgb[3][3] = {{pcspace->red.x, pcspace->green.x, pcspace->blue.x},
                         {pcspace->red.y, pcspace->green.y, pcspace->blue.y},
                         {z[1], z[2], z[3]}};
  double mat1[3][3];
  meta_color_utility_matrix_inverse_3x3 (xyzrgb, mat1);
  meta_color_utility_matrix_multi_3x3_with_3x1 (mat1, XYZw, XYZsum);
  double mat2[3][3] = {{XYZsum[0], 0, 0}, {0, XYZsum[1], 0}, {0, 0, XYZsum[2]}};
  meta_color_utility_matrix_multi_3x3 (xyzrgb, mat2, rgb2xyz);
}

void meta_color_utility_create_gamut_scaling_matrix (MetaColorSpace *psrc,
						     MetaColorSpace *pdst,
						     double result[3][3])
{
  double mat1[3][3], mat2[3][3], tmp[3][3];
  meta_color_utility_create_rgb2xyz_matrix (psrc, mat1);
  meta_color_utility_create_rgb2xyz_matrix (pdst, mat2);

  meta_color_utility_matrix_inverse_3x3 (mat2, tmp);
  meta_color_utility_matrix_multi_3x3 (tmp, mat1, result);
}

void meta_color_utility_get_transformation_matrix (MetaColorSpace srccolorspace,
                                                   MetaColorSpace destcolorspace,
                                                   MetaCtm *ctm)
{
  /*
   https://en.wikipedia.org/wiki/Rec._2020#System_colorimetry
   https://en.wikipedia.org/wiki/Rec._709#Primary_chromaticities
  */
  double result[3][3];

  meta_color_utility_create_gamut_scaling_matrix (&srccolorspace, &destcolorspace, result);

  for (uint8_t y = 0, z = 0; y < 3; y++)
    {
      for (uint8_t x = 0; x < 3; x++)
        {
          if (result[y][x] < 0)
            {
              ctm->matrix[z] = (int64_t) (-result[y][x] * ((int64_t) 1L << 32));
              ctm->matrix[z] |= 1ULL << 63;
            }
          else
              ctm->matrix[z] =  (int64_t) (result[y][x] * ((int64_t) 1L << 32));
          z++;
        }
    }
}

double meta_color_utility_oetf_2084 (double input,
				     double srcmaxluminance)
{
  double cf = 1.0f;
  double output = 0.0f;
  if (input != 0.0f)
    {
      cf = srcmaxluminance / 10000.0;
      input *= cf;
      output = pow (((c1 + (c2 * pow (input, m1))) / (1 + (c3 * pow (input, m1)))), m2);
    }
  return output;
}

/*
* Reference: https://nick-shaw.github.io/cinematiccolor/common-rgb-color-spaces.html
*
* The ST 2084 EOTF is an absolute encoding, defined by the following equation:
*         L=10000×{max(V^1∕m2−c1,0)/(c2−c3×V^1∕m2)}^1∕m1
* where constants are:
*         m1 = 2610∕16384
*         m2 = 2523∕4096×128
*         c1 = 3424∕4096
*         c2 = 2413∕4096×32
*         c3 = 2392∕4096×32
*/
double meta_color_utility_eotf_2084 (double input)
{
  double output = 0.0f;
  if (input != 0.0f)
    {
      output = pow (((fmax((pow (input, (1.0 / m2)) - c1), 0)) / (c2 - (c3 * pow (input, (1.0 / m2))))), (1.0 / m1));
    }
  return output;
}

void meta_color_utility_generate_oetf_2084_lut (MetaOneDLUT *lut)
{
  for (int i = 0; i < lut->nSamples; i++)
    {
      lut->pLutData[i] = (double)i / (double)(lut->nSamples - 1);
      lut->pLutData[i] = meta_color_utility_oetf_2084(lut->pLutData[i], 10000.0);
    }
}

void meta_color_utility_generate_eotf_2084_lut (MetaOneDLUT *lut)
{
  for (int i = 0; i < lut->nSamples; i++)
    {
      lut->pLutData[i] = (double)i / (double)(lut->nSamples - 1);
      lut->pLutData[i] = meta_color_utility_eotf_2084(lut->pLutData[i]);
    }
}

void meta_color_utility_populate_standard_lut (char *standardlutkey,
					       MetaOneDLUT *lut)
{
  if (!strcmp(standardlutkey, "eotf2084"))
    meta_color_utility_generate_eotf_2084_lut(lut);
  else if (!strcmp(standardlutkey, "oetf2084"))
    meta_color_utility_generate_oetf_2084_lut(lut);
  else
    printf("This custom CURVE is not supported for LUT values");
}

double meta_color_utility_get_srgb_decoding_value (double input)
{
  /*
   https://en.wikipedia.org/wiki/SRGB#The_forward_transformation_.28CIE_xyY_or_CIE_XYZ_to_sRGB.29
  */
  double output = 0.0f;
  if (input <= 0.004045f)
    output = input / 12.92f;
  else
    output = pow (((input + 0.055) / 1.055), 2.4);
  return output;
}

void meta_color_utility_generate_srgb_degamma_lut (MetaDegammaLut *degamma)
{
  uint32_t max_val = (1 << 16) - 1;
  for (int i=0; i < degamma->size; i++)
    {
      double normalized_input = (double)i / (double)(degamma->size - 1);
      degamma->red[i] = (double)max_val * meta_color_utility_get_srgb_decoding_value (normalized_input) + 0.5;

      if (degamma->red[i] > max_val)
        degamma->red[i] = max_val;
      degamma->green[i] = degamma->blue[i] = degamma->red[i];
    }
}

double meta_color_utility_get_srgb_encoding_value (double input)
{
  double output = 0.0f;
  if (input <= 0.0031308f)
    output = input * 12.92;
  else
    output = (1.055 * pow (input, 1.0 / 2.4)) - 0.055;
  return output;
}

void meta_color_utility_generate_srgb_gamma_lut (MetaGammaLut *gamma)
{
  uint32_t max_val = (1 << 16) - 1;
  for (int i=0; i < gamma->size; i++)
    {
      double normalized_input = (double)i / (double)(gamma->size - 1);
      gamma->red[i] = (double)max_val * meta_color_utility_get_srgb_encoding_value (normalized_input) + 0.5;

      if (gamma->red[i] > max_val)
        gamma->red[i] = max_val;
      gamma->green[i] = gamma->blue[i] = gamma->red[i];
    }
}

