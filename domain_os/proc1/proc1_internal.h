/*
 * proc1/proc1_internal.h - Internal PROC1 Definitions
 *
 * Contains internal functions, data, and types used only within
 * the proc1 subsystem. External consumers should use proc1/proc1.h.
 */

#ifndef PROC1_INTERNAL_H
#define PROC1_INTERNAL_H

#include "proc1/proc1.h"

/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * FUN_00e20824 - Unknown internal function
 *
 * Called after removing a process from the ready list during
 * deferred operation handling. Purpose unknown.
 *
 * TODO: Identify and rename this function.
 *
 * Original address: 0x00e20824
 */
void FUN_00e20824(void);

/*
 * proc1_$clr_lock_int - Internal clear lock implementation
 *
 * Called with interrupts disabled to release a resource lock.
 *
 * Parameters:
 *   lock_id - Lock ID (0-31)
 *
 * Original address: 0x00e20b92 (part of PROC1_$CLR_LOCK)
 */
void proc1_$clr_lock_int(uint16_t lock_id);

/*
 * ============================================================================
 * Internal Data Declarations
 * ============================================================================
 */

/*
 * PROC1_$VT_TIMER_DATA - Virtual timer callback data
 *
 * Data structure used with TIME_$WRT_VT_TIMER for virtual timer
 * management. Exact format TBD.
 *
 * Original address: 0xe14a06
 */
extern char PROC1_$VT_TIMER_DATA[];

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */

/*
 * Illegal process ID error - used when a function is called
 * with an invalid PID (0 or > 64).
 */
extern const status_$t Illegal_PID_Err;

#endif /* PROC1_INTERNAL_H */
