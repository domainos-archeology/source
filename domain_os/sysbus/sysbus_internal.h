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

/* The two first-level interrupt routines SYSBUS_$INIT installs. */
#include "disk/disk.h"          /* DISK_INTERRUPT (disk/interrupt.c) */
#include "ring/ring.h"          /* RING_INTERRUPT (ring/interrupt.c) */

#endif /* SYSBUS_INTERNAL_H */
