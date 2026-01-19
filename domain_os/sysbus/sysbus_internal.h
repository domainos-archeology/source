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
 * Error status for unknown interrupt ID
 * Used by SYSBUS_$DEFINE_INT when called unexpectedly.
 */
extern const status_$t Sysbus_Unknown_Interrupt_ID_Err;

/*
 * External interrupt handler declarations
 * These are defined in the disk and ring subsystems.
 */
extern void DISK_INTERRUPT(void);
extern void RING_INTERRUPT(void);

#endif /* SYSBUS_INTERNAL_H */
