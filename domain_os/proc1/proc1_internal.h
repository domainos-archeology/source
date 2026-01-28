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
 * INIT_STACK - Initialize process stack for first dispatch
 *
 * Sets up a new process's initial stack so that when the
 * dispatcher context-switches to it, the process will begin
 * execution at the specified entry point.
 *
 * Parameters:
 *   pcb - Pointer to process control block
 *   entry_ptr - Pointer to entry point address
 *   sp_ptr - Pointer to stack pointer value
 *
 * Original address: 0x00E20AA4 (40 bytes)
 */
void INIT_STACK(proc1_t *pcb, void **entry_ptr, void **sp_ptr);

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
 * proc1_$set_lock_body - Internal set lock implementation (assembly)
 *
 * Internal entry point for PROC1_$SET_LOCK, called with lock_id in D0.
 * Increments lock depth (PCB+0x5A), checks ordering, sets lock bit.
 *
 * Original address: 0x00e20ae8
 */
void proc1_$set_lock_body(void);

/*
 * proc1_$clr_lock_body - Internal clear lock implementation (assembly)
 *
 * Internal entry point for PROC1_$CLR_LOCK, called with lock_id in D0,
 * current PCB in A1, and interrupts disabled.
 * Clears lock bit, decrements lock depth, handles deferred operations.
 *
 * Original address: 0x00e20b9e
 */
void proc1_$clr_lock_body(void);

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
 * Defined in misc/crash_system.c
 */
extern status_$t Illegal_PID_Err;

#endif /* PROC1_INTERNAL_H */
