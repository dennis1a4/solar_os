#pragma once
#include "solar_os_organizer_model.h"
#include <stdio.h>
bool pim_read(FILE *, pim_db_t *, char *error, size_t);
bool pim_write(FILE *, const pim_db_t *);
/* Interchange is a bounded local/floating-time ICS and vCard 3.0 subset.
 * Caller imports into a transaction copy and commits only if all records pass.
 */
bool pim_export(FILE *, const pim_db_t *, unsigned kind);
bool pim_import(FILE *, pim_db_t *, unsigned kind, unsigned *count, char *error,
                size_t);
