#include "solar_os_units.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
/* Definitions follow NIST SP 811 Appendix B; hp is mechanical, BTU is IT. */
#define U(S, C, F) {S, C, F, 0}
static const solar_unit_t units[] = {
    U("m", "Length", 1),
    U("mm", "Length", .001),
    U("cm", "Length", .01),
    U("km", "Length", 1000),
    U("in", "Length", .0254),
    U("ft", "Length", .3048),
    U("yd", "Length", .9144),
    U("mi", "Length", 1609.344),
    U("nmi", "Length", 1852),
    U("mil", "Length", .0000254),
    U("m2", "Area", 1),
    U("cm2", "Area", .0001),
    U("ft2", "Area", .09290304),
    U("ha", "Area", 10000),
    U("acre", "Area", 4046.8564224),
    U("L", "Volume", 1),
    U("mL", "Volume", .001),
    U("m3", "Volume", 1000),
    U("USgal", "Volume", 3.785411784),
    U("UKgal", "Volume", 4.54609),
    U("USfloz", "Volume", .0295735295625),
    U("kg", "Mass", 1),
    U("g", "Mass", .001),
    U("mg", "Mass", .000001),
    U("lb", "Mass", .45359237),
    U("oz", "Mass", .028349523125),
    U("tonne", "Mass", 1000),
    {"C", "Temperature", 1, 273.15},
    {"K", "Temperature", 1, 0},
    {"F", "Temperature", 5. / 9., 273.15 - 32 * 5. / 9.},
    U("m/s", "Speed", 1),
    U("km/h", "Speed", 1. / 3.6),
    U("mph", "Speed", .44704),
    U("knot", "Speed", 1852. / 3600),
    U("ft/s", "Speed", .3048),
    U("Pa", "Pressure", 1),
    U("kPa", "Pressure", 1000),
    U("MPa", "Pressure", 1000000),
    U("bar", "Pressure", 100000),
    U("psi", "Pressure", 6894.757293168),
    U("atm", "Pressure", 101325),
    U("mmHg", "Pressure", 133.322387415),
    U("N", "Force", 1),
    U("kN", "Force", 1000),
    U("lbf", "Force", 4.4482216152605),
    U("J", "Energy", 1),
    U("kJ", "Energy", 1000),
    U("Wh", "Energy", 3600),
    U("kWh", "Energy", 3600000),
    U("cal", "Energy", 4.184),
    U("kcal", "Energy", 4184),
    U("BTU", "Energy", 1055.05585262),
    U("W", "Power", 1),
    U("kW", "Power", 1000),
    U("mW", "Power", .001),
    U("hp", "Power", 745.699871582),
    U("Nm", "Torque", 1),
    U("lb-ft", "Torque", 1.3558179483314),
    U("s", "Time", 1),
    U("ms", "Time", .001),
    U("us", "Time", .000001),
    U("min", "Time", 60),
    U("h", "Time", 3600),
    U("day", "Time", 86400),
    U("Hz", "Frequency", 1),
    U("kHz", "Frequency", 1000),
    U("MHz", "Frequency", 1000000),
    U("GHz", "Frequency", 1e9),
    U("V", "Voltage", 1),
    U("mV", "Voltage", .001),
    U("kV", "Voltage", 1000),
    U("A", "Current", 1),
    U("mA", "Current", .001),
    U("uA", "Current", .000001),
    U("ohm", "Resistance", 1),
    U("mohm", "Resistance", .001),
    U("kohm", "Resistance", 1000),
    U("Mohm", "Resistance", 1000000),
    U("Fcap", "Capacitance", 1),
    U("mF", "Capacitance", .001),
    U("uF", "Capacitance", 1e-6),
    U("nF", "Capacitance", 1e-9),
    U("pF", "Capacitance", 1e-12),
    U("H", "Inductance", 1),
    U("mH", "Inductance", .001),
    U("uH", "Inductance", 1e-6),
    U("rad", "Angle", 1),
    U("deg", "Angle", .017453292519943295),
    U("byte", "Data", 1),
    U("bit", "Data", .125),
    U("kB", "Data", 1000),
    U("MB", "Data", 1000000),
    U("KiB", "Data", 1024),
    U("MiB", "Data", 1048576)};
size_t solar_units_count(void) { return sizeof(units) / sizeof(units[0]); }
const solar_unit_t *solar_units_get(size_t n) {
  return n < solar_units_count() ? &units[n] : NULL;
}
bool solar_units_convert(double value, const char *from, const char *to,
                         double *out, char *error, size_t n) {
  const solar_unit_t *a = NULL, *b = NULL;
  for (size_t i = 0; i < solar_units_count(); i++) {
    if (!strcmp(from, units[i].symbol))
      a = &units[i];
    if (!strcmp(to, units[i].symbol))
      b = &units[i];
  }
  if (!a || !b) {
    snprintf(error, n, "Unknown symbol (case matters); use the unit list");
    return false;
  }
  if (strcmp(a->category, b->category)) {
    snprintf(error, n, "Units belong to different dimensions");
    return false;
  }
  double si = value * a->scale + a->offset,
         result = (si - b->offset) / b->scale;
  if (!isfinite(value) || !isfinite(si) || !isfinite(result)) {
    snprintf(error, n, "Number or result out of range");
    return false;
  }
  if (!strcmp(a->category, "Temperature") && si < -1e-10) {
    snprintf(error, n, "Temperature below absolute zero");
    return false;
  }
  *out = result;
  snprintf(error, n, "%s", a->category);
  return true;
}
