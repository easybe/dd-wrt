/*
 * Memory allocation unit
 *
 * $Id: mem.h,v 1.7 1994/11/10 10:00:26 asdf Exp $
 *
 */

#ifndef LANE_MEM_H
#define LANE_MEM_H

/* System includes needed for types */
#include "sys/types.h"

/* Local includes needed for types */
#include "units.h"

/* Type definitions */

/* Global function prototypes */
void *mem_alloc(const Unit_t *unit, size_t nbytes);
void mem_free(const Unit_t *unit, const void *mem);

/* Dump memory allocation info, NULL == all */
void mem_dump(const Unit_t *unit);

/* Global data */
extern const Unit_t mem_unit;

#endif

