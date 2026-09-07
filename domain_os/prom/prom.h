/*
 * PROM - PROM/Boot ROM Interface
 *
 * This module provides interfaces to the Apollo boot PROM.
 */

#ifndef PROM_H
#define PROM_H

#include "base/base.h"
#include "io/io.h"   /* io_$probe */

/*
 * PROM entry points and data
 */

/* PROM warm restart entry point - used for clean shutdown */
extern void *PROM_$QUIET_RET_ADDR;

/*
 * PROM_$MACHINE_ID - machine identification longword the boot PROM leaves in
 * the trap page.
 *
 * The SAU2 map names 0x00000100 PROM_$MACHINE_ID, inside the "D37 0 TRAP_PAGE
 * loaded at 0, size = 400" segment; the loaded image carries no bytes for it
 * (the PROM writes it), so on the m68k it is an absolute-address cell rather
 * than an object the kernel defines.  GET_BUILD_TIME reads only its high word
 * (`move.w (0x00000100).l,D2w` at 0x00E380D6), the SAU-and-aux code.
 */
#if defined(ARCH_M68K)
#define PROM_$MACHINE_ID (*(const volatile uint32_t *)0x00000100)
#else
extern uint32_t PROM_$MACHINE_ID;
#endif

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
/* io_$probe is declared in io/io.h (bead source-3uo). */

#endif /* PROM_H */
