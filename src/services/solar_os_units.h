#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {
  const char *symbol, *category;
  double scale, offset;
} solar_unit_t;
size_t solar_units_count(void);
const solar_unit_t *solar_units_get(size_t);
bool solar_units_convert(double, const char *, const char *, double *, char *,
                         size_t);
