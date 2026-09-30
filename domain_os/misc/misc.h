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

/*
 * GET_WIRED - the address of the AUDIT wired segment
 *
 * AUDIT's only routine in the SAU2 map ("I E1D8DC AUDIT size = 18"): a
 * Pascal function that returns 0xE2E07C, the AUDIT wired data segment
 * (audit_$wired_ec, audit/audit.h), in A0.  Sole caller AUDIT_$INIT
 * (0x00E70B08).
 *
 * Original address: 0x00E1D8DC (22 bytes)
 */
void *GET_WIRED(void);

/*
 * PRINT_BUILD_TIME - print the kernel's build banner on the console
 *
 * GET_BUILD_TIME into a local buffer, then VFMT_$WRITE10("%/%a%/%.").
 * Called by OS_$INIT (0x00E33D2A).
 *
 * Original address: 0x00E38000 (38 bytes)
 */
void PRINT_BUILD_TIME(void);

/*
 * CHK - is a controller of this type configured and up?
 *
 * Module-local routine of the IO_ code segment (map "I E1A404 IO_", symbol
 * CHK): walks IO_$DCTE_LIST for a DCTE whose ctype equals *ctype and
 * returns a Domain boolean, true (0xFF, `seq') when such a DCTE's cstatus
 * is zero.  Called three times by IO_$GET_CONFIG.
 *
 * Original address: 0x00E1A404 (68 bytes)
 */
int8_t CHK(const uint16_t *ctype);

#endif /* MISC_H */
