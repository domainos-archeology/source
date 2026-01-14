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

#endif /* KBD_H */
