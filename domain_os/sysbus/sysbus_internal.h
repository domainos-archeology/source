/*
 * sysbus/sysbus_internal.h - System Bus Internal Definitions
 *
 * Internal header for SYSBUS subsystem implementation.
 */

#ifndef SYSBUS_INTERNAL_H
#define SYSBUS_INTERNAL_H

#include "sysbus/sysbus.h"
#include "misc/crash_system.h"

/*
 * The status cell SYSBUS_$DEFINE_INT crashes with: the longword at
 * 0x00E0ABD0 (`pea (0xe,PC)` at 0x00E0ABC0), bytes 00 3e 00 02, "unknown
 * interrupt ID" in the SR10.2 status database (module 0x3E).
 */
#define status_$sysbus_unknown_interrupt_id 0x003E0002

/*
 * External interrupt handler declarations
 * These are defined in the disk and ring subsystems.
 */
extern void DISK_INTERRUPT(void);
extern void RING_INTERRUPT(void);

#endif /* SYSBUS_INTERNAL_H */
