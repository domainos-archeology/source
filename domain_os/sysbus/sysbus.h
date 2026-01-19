/*
 * sysbus/sysbus.h - System Bus Public API
 *
 * The SYSBUS subsystem manages system bus interrupt handling and
 * device controller table entry (DCTE) initialization. It sets up
 * interrupt handlers for disk and ring network interrupts.
 *
 * Original addresses: 0x00e0ab28 - 0x00e0abcc
 */

#ifndef SYSBUS_H
#define SYSBUS_H

#include "base/base.h"
#include "io/io.h"

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * SYSBUS_$INIT - Initialize system bus interrupt handlers
 *
 * Sets up interrupt vectors for disk (0x1d) and ring (0x1b) interrupts,
 * then iterates through the device controller table entry (DCTE) list
 * to populate the interrupt controller data structures for each
 * controller type:
 *   - Type 0: Secondary disk controller
 *   - Type 1: Primary disk controller
 *   - Type 2: Another controller type
 *
 * Original address: 0x00e0ab28
 */
void SYSBUS_$INIT(void);

/*
 * SYSBUS_$DEFINE_INT - Define an interrupt (stub/error handler)
 *
 * This function crashes the system with "Unknown Interrupt ID" error.
 * It appears to be a placeholder for an unimplemented feature.
 *
 * Original address: 0x00e0abbc
 */
void SYSBUS_$DEFINE_INT(void);

#endif /* SYSBUS_H */
