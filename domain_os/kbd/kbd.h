/*
 * KBD - Keyboard Controller
 *
 * This module provides keyboard controller operations.
 */

#ifndef KBD_H
#define KBD_H

#include "base/base.h"

/*
 * KBD_$RESET - Reset the keyboard controller
 *
 * Resets the keyboard controller hardware to a known state.
 * Called during crash handling and system shutdown.
 *
 * Original address: TBD
 */
void KBD_$RESET(void);

/*
 * KBD_$CRASH_INIT - Initialize keyboard for crash console
 *
 * Initializes the keyboard controller for use during crash handling.
 * Sets up basic input capability for the crash debugger.
 *
 * Original address: TBD
 */
void KBD_$CRASH_INIT(void);

/*
 * KBD_$SET_KBD_MODE - Set keyboard mode
 *
 * Sets the keyboard mode for a terminal line.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   mode     - Pointer to keyboard mode value
 *   status   - Pointer to receive status code
 *
 * Original address: TBD
 */
void KBD_$SET_KBD_MODE(short *line_ptr, unsigned char *mode, status_$t *status);

/*
 * KBD_$PUT - Write keyboard string
 *
 * Sends a keyboard string to the terminal subsystem.
 *
 * Parameters:
 *   param1   - First parameter (buffer/state)
 *   param2   - Second parameter (buffer/state)
 *   str      - String to send
 *   length   - Length of string
 *   status   - Pointer to receive status code
 *
 * Original address: TBD
 */
void KBD_$PUT(void *param1, void *param2, void *str, void *length, status_$t *status);

#endif /* KBD_H */
