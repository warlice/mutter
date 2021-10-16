#include <clutter/clutter-units.h>

#include "tests/clutter-test-utils.h"

static void
units_constructors (void)
{
  ClutterUnits units;

  clutter_units_from_pixels (&units, 100);
  g_assert (clutter_units_get_unit_type (&units) == CLUTTER_UNIT_PIXEL);
  g_assert_cmpfloat (clutter_units_get_unit_value (&units), ==, 100.0);
  g_assert_cmpfloat (clutter_units_to_pixels (&units), ==, 100.0);
}

static void
units_string (void)
{
  ClutterUnits units;

  g_assert (clutter_units_from_string (&units, "") == FALSE);

  g_assert (clutter_units_from_string (&units, "10") == TRUE);
  g_assert (clutter_units_get_unit_type (&units) == CLUTTER_UNIT_PIXEL);
  g_assert_cmpfloat (clutter_units_get_unit_value (&units), ==, 10);

  g_assert (clutter_units_from_string (&units, "10 px") == TRUE);
  g_assert (clutter_units_get_unit_type (&units) == CLUTTER_UNIT_PIXEL);

  g_assert (clutter_units_from_string (&units, "10  ") == TRUE);
  g_assert (clutter_units_get_unit_type (&units) == CLUTTER_UNIT_PIXEL);
  g_assert_cmpfloat (clutter_units_get_unit_value (&units), ==, 10);

  g_assert (clutter_units_from_string (&units, "  32   em   garbage") == FALSE);

  g_assert (clutter_units_from_string (&units, "1 omg!!pony") == FALSE);

  units.unit_type = CLUTTER_UNIT_PIXEL;
  units.value = 0;
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/units/string", units_string)
  CLUTTER_TEST_UNIT ("/units/constructors", units_constructors)
)
