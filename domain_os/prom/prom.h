/*
 * PROM - PROM/Boot ROM Interface
 *
 * This module provides interfaces to the Apollo boot PROM.
 */

#ifndef PROM_H
#define PROM_H

#include "base/base.h"

/*
 * PROM entry points and data
 */

/* PROM warm restart entry point - used for clean shutdown */
extern void *PROM_$QUIET_RET_ADDR;

/* PROM data */
extern uint32_t PROM_$SAU_AND_AUX;

/*
 * io_$probe - Hardware probe function
 *
 * Probes for hardware controller presence at a given address.
 * Sets bus error handler, disables interrupts, clears MMU status,
 * and dispatches through a jump table based on controller type.
 *
 * Parameters:
 *   type   - Pointer to controller type variable
 *   addr   - Hardware address to probe
 *   result - Buffer for probe result data
 *
 * Returns:
 *   Negative if hardware found, non-negative if not present
 *
 * Original address: 0x00e29138
 */
int8_t io_$probe(void *type, void *addr, void *result);

#endif /* PROM_H */
