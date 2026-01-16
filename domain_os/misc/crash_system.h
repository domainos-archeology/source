/*
 * misc/crash_system.h - Fatal System Crash Handler
 *
 * Provides the CRASH_SYSTEM function for handling unrecoverable errors.
 */

#ifndef MISC_CRASH_SYSTEM_H
#define MISC_CRASH_SYSTEM_H

#include "base/base.h"

/*
 * CRASH_SYSTEM - Fatal system crash handler
 *
 * Called when the kernel encounters an unrecoverable error.
 * Saves system state, displays error info, and either reboots
 * or enters the crash debugger.
 *
 * @param status_p: Pointer to status code that caused the crash
 *
 * This function does not return.
 */
extern void CRASH_SYSTEM(const status_$t *status_p);

/*
 * Crash data - available for debugging after crash
 */
extern status_$t CRASH_STATUS;
extern void *CRASH_ECB;
extern uint32_t CRASH_REGS[16];
extern uint32_t CRASH_USP;

/*
 * Standard error codes used with CRASH_SYSTEM
 */
extern status_$t Lock_ordering_violation;
extern status_$t Illegal_lock_err;
extern status_$t Lock_order_violation_err;
extern status_$t No_calendar_on_system_err;
extern status_$t OS_BAT_disk_needs_salvaging_err;
extern status_$t No_err;
extern status_$t PMAP_VM_Resources_exhausted_err;
extern status_$t MST_Ref_OutOfBounds_Err;
extern status_$t Disk_Queued_Drivers_Not_Supported_Err;
extern status_$t Disk_Driver_Logic_Err;
extern status_$t Disk_controller_err;
extern status_$t Disk_driver_logic_err;
extern status_$t Illegal_PID_Err;
extern status_$t Illegal_WSL_Index_Err;
extern status_$t WSL_Exhausted_Err;
extern status_$t Inconsistent_MMAPE_Err;
extern status_$t MMAP_Bad_Unavail_err;
extern status_$t mmap_bad_avail;
extern status_$t MMAP_Bad_Reclaim_Err;
extern status_$t MMAP_Error_Examined_Max;
extern status_$t Some_ASTE_Error;
extern status_$t OS_PMAP_mismatch_err;
extern status_$t OS_MMAP_bad_install;

#endif /* MISC_CRASH_SYSTEM_H */
