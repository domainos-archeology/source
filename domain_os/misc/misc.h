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

/* NOTE: VFMT_$WRITE10 is declared in vfmt/vfmt.h */

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
 * DISP_LITES - Draw the row of 16 status-light blocks on the display
 *
 * Low-level display routine (not yet decompiled) wrapped by SMD_$LITES.
 *
 * Parameters:
 *   pattern - 16-bit light pattern
 *   y_pos   - Display row
 *
 * Original address: 0x00E1E9F4
 */
void DISP_LITES(uint16_t pattern, uint16_t y_pos);

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
