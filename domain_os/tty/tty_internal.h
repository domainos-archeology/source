/*
 * tty/tty_internal.h - Internal TTY Definitions
 *
 * Contains internal functions and data used only within
 * the TTY subsystem. External consumers should use tty/tty.h.
 */

#ifndef TTY_INTERNAL_H
#define TTY_INTERNAL_H

#include "tty/tty.h"

/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * FUN_00e1aef8 - Advance eventcount (internal)
 *
 * Advances an eventcount and wakes any waiters.
 * This is used internally for TTY synchronization.
 *
 * Parameters:
 *   ec - Pointer to eventcount to advance
 *
 * Original address: 0x00e1aef8
 */
void FUN_00e1aef8(m68k_ptr_t ec);

/*
 * FUN_00e1bcfc - Error handling helper
 *
 * Internal error handling for TTY operations.
 *
 * Original address: 0x00e1bcfc
 */
void FUN_00e1bcfc(void);

/*
 * FUN_00e1bf70 - Set raw mode helper
 *
 * Internal helper for setting TTY raw mode.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   raw - Raw mode flag (0 = cooked, non-zero = raw)
 *
 * Original address: 0x00e1bf70
 */
void FUN_00e1bf70(tty_desc_t *tty, char raw);

/*
 * FUN_00e1aed0 - Lock TTY
 *
 * Acquires the TTY lock for exclusive access.
 *
 * Parameters:
 *   tty - TTY descriptor to lock
 *
 * Original address: 0x00e1aed0
 */
void FUN_00e1aed0(tty_desc_t *tty);

/*
 * FUN_00e1aee4 - Unlock TTY
 *
 * Releases the TTY lock.
 *
 * Parameters:
 *   tty - TTY descriptor to unlock
 *
 * Original address: 0x00e1aee4
 */
void FUN_00e1aee4(tty_desc_t *tty);

/*
 * FUN_00e6720e - Set break character
 *
 * Configures break character handling for TTY.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   break_char - Break character code
 *   enable - Enable/disable break handling
 *
 * Original address: 0x00e6720e
 */
void FUN_00e6720e(tty_desc_t *tty, uint8_t break_char, char enable);

/*
 * FUN_00e6726e - Update TTY state
 *
 * Updates TTY internal state.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   update_all - Whether to update all state
 *
 * Original address: 0x00e6726e
 */
void FUN_00e6726e(tty_desc_t *tty, char update_all);

/*
 * ============================================================================
 * Internal Data Declarations
 * ============================================================================
 */

/*
 * DAT_00e82454 - Default break character
 *
 * Default character used for break handling.
 * Original address: 0x00e82454
 */
extern uint8_t DAT_00e82454;

/*
 * PTR_TTY_$I_DXM_SIGNAL - Pointer to TTY_$I_DXM_SIGNAL function
 *
 * Used for DXM callback registration.
 */
extern m68k_ptr_t PTR_TTY_$I_DXM_SIGNAL;

#endif /* TTY_INTERNAL_H */
