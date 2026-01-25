/*
 * misc/misc.h - Miscellaneous Kernel Functions
 *
 * This module contains kernel functions that don't belong to
 * a specific subsystem, such as crash handling and system utilities.
 */

#ifndef MISC_H
#define MISC_H

#include "base/base.h"
#include "vfmt/vfmt.h"

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
void CRASH_SYSTEM(const status_$t *status_p);

/* NOTE: ERROR_$PRINT is declared in vfmt/vfmt.h */

/*
 * Common error codes used with CRASH_SYSTEM
 */
// extern status_$t Lock_ordering_violation;
// extern status_$t Illegal_lock_err;
// extern status_$t Lock_order_violation_err;
// extern status_$t No_calendar_on_system_err;
// extern status_$t OS_BAT_disk_needs_salvaging_err;
// extern status_$t No_err;
// extern status_$t PMAP_VM_Resources_exhausted_err;
// extern status_$t MST_Ref_OutOfBounds_Err;
// extern status_$t Disk_Queued_Drivers_Not_Supported_Err;
// extern status_$t Disk_Driver_Logic_Err;
// extern status_$t Disk_controller_err;
// extern status_$t Disk_driver_logic_err;
// extern status_$t Illegal_PID_Err;
// extern status_$t Illegal_WSL_Index_Err;
// extern status_$t WSL_Exhausted_Err;
// extern status_$t Inconsistent_MMAPE_Err;
// extern status_$t MMAP_Bad_Unavail_err;
// extern status_$t mmap_bad_avail;
// extern status_$t MMAP_Bad_Reclaim_Err;
// extern status_$t MMAP_Error_Examined_Max;
// extern status_$t Some_ASTE_Error;
// extern status_$t OS_PMAP_mismatch_err;
// extern status_$t OS_MMAP_bad_install;

/*
 * prompt_for_yes_or_no - Prompt user for yes/no answer
 *
 * Reads from terminal and waits for user to enter Y/y (yes) or N/n (no).
 * Loops with error message until valid response is given.
 *
 * Returns:
 *   0xff (true)  - User answered yes
 *   0x00 (false) - User answered no
 *
 * Original address: 0x00e33778
 */
uint8_t prompt_for_yes_or_no(void);

/*
 * SET_LITES_LOC - Set memory-mapped status lights location
 *
 * Sets the location where the kernel should display status lights
 * (memory activity indicators). If the lights were previously disabled
 * (location was 0) and a valid location is provided, starts the
 * MEM_LITES process to update the display.
 *
 * Parameters:
 *   loc_p - Pointer to the new lights location (display memory address)
 *
 * The lights location is typically a display memory address where
 * 16 status indicator blocks can be drawn to show system activity.
 *
 * Original address: 0x00e0c4dc
 */
void SET_LITES_LOC(int32_t *loc_p);

/*
 * GET_BUILD_TIME - Get kernel build version string
 *
 * Formats the kernel version information into a buffer. The output
 * includes SAU type, revision numbers, and build timestamp.
 *
 * Format examples:
 *   "Domain/OS kernel, revision 10.4.2"           (no SAU)
 *   "Domain/OS kernel(2), revision 10.4.2,..."    (with SAU type)
 *
 * Parameters:
 *   buf   - Output buffer (minimum 100 bytes recommended)
 *   len_p - Pointer to receive actual string length
 *
 * Returns:
 *   "?" if OS_$REV is non-zero (invalid/test build)
 *
 * Original address: 0x00e38052
 */
void GET_BUILD_TIME(char *buf, int16_t *len_p);

#endif /* MISC_H */
