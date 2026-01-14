/*
 * misc/misc.h - Miscellaneous Kernel Functions
 *
 * This module contains kernel functions that don't belong to
 * a specific subsystem, such as crash handling and system utilities.
 */

#ifndef MISC_H
#define MISC_H

#include "base/base.h"

/*
 * CRASH_SYSTEM - Fatal system crash handler
 *
 * Called when the kernel encounters an unrecoverable error.
 * Saves system state, displays error info, and either reboots
 * or enters the crash debugger (trap #15).
 *
 * Special cases:
 *   status_$ok (0) - Clean shutdown, returns to PROM
 *   status_$system_reboot (0x1b0008) - Clean reboot
 *
 * @param status_p  Pointer to status code that caused the crash
 *
 * Original address: 0x00E1E700
 */
void CRASH_SYSTEM(status_$t *status_p);

/*
 * Common error codes used with CRASH_SYSTEM
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

/*
 * Common error strings used with CRASH_SYSTEM
 */
extern const char *Illegal_PID_Err;
extern const char *Illegal_WSL_Index_Err;
extern const char *WSL_Exhausted_Err;
extern const char *Inconsistent_MMAPE_Err;
extern const char *MMAP_Bad_Unavail_err;
extern const char *mmap_bad_avail;
extern const char *MMAP_Bad_Reclaim_Err;
extern const char *MMAP_Error_Examined_Max;
extern const char *Some_ASTE_Error;
extern const char *OS_PMAP_mismatch_err;
extern const char *OS_MMAP_bad_install;

#endif /* MISC_H */
