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
 * TTY_$I_ADVANCE_EC - Advance eventcount without dispatch
 *
 * Advances an eventcount and wakes any waiters without
 * triggering a process dispatch.
 *
 * Parameters:
 *   ec - Pointer to eventcount to advance
 *
 * Original address: 0x00e1aef8
 */
void TTY_$I_ADVANCE_EC(m68k_ptr_t ec);

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
 * TTY_$I_LOCK - Lock TTY
 *
 * Acquires the TTY lock (lock ID 3) for exclusive access.
 *
 * Parameters:
 *   tty - TTY descriptor (unused - lock is global)
 *
 * Original address: 0x00e1aed0
 */
void TTY_$I_LOCK(tty_desc_t *tty);

/*
 * TTY_$I_XMIT_CHAR - Transmit a single character
 *
 * Outputs a single character to the TTY output buffer.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - Character to transmit
 *
 * Original address: 0x00e1b3b4
 */
void TTY_$I_XMIT_CHAR(tty_desc_t *tty, uint16_t ch);

/*
 * TTY_$I_UNLOCK - Unlock TTY
 *
 * Releases the TTY lock (lock ID 3).
 *
 * Parameters:
 *   tty - TTY descriptor (unused - lock is global)
 *
 * Original address: 0x00e1aee4
 */
void TTY_$I_UNLOCK(tty_desc_t *tty);

/*
 * TTY_$I_ECHO_CHAR - Echo a character to the TTY
 *
 * Echoes a character to the terminal output. Control characters
 * (except TAB and LF) are displayed with a caret prefix (e.g., ^C).
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - Character to echo
 *
 * Original address: 0x00e1b3ce
 */
void TTY_$I_ECHO_CHAR(tty_desc_t *tty, uint8_t ch);

/*
 * tty_$i_set_funcs - Set function character class entries
 *
 * Iterates over all function character slots (0..17) and updates
 * the character class table. For each enabled function, sets
 * the corresponding char_class entry to either the default class
 * (from the signal table via A5) or 0x12 (normal).
 *
 * Parameters:
 *   tty       - TTY descriptor
 *   func_mask - Bitmask of which functions to set
 *   use_dfl   - If negative (true), use default class values;
 *               otherwise set all to NORMAL (0x12)
 *
 * Original address: 0x00e6720e
 */
void tty_$i_set_funcs(tty_desc_t *tty, uint32_t func_mask, char use_dfl);

/*
 * TTY_$I_SET_DFL_FUNCS - Set default function character classes
 *
 * Wrapper around tty_$i_set_funcs that passes the default
 * enabled function mask (DAT_00e82450) from the TTY global data.
 *
 * Parameters:
 *   tty       - TTY descriptor
 *   use_dfl   - If negative (true), use default class values
 *
 * Original address: 0x00e6726e
 */
void TTY_$I_SET_DFL_FUNCS(tty_desc_t *tty, char use_dfl);

/*
 * tty_$i_put_chars - Put characters into TTY output buffer
 *
 * Processes a string of characters for TTY output. Handles special
 * characters: BS (0x08), CR (0x0D), LF (0x0A), TAB (0x09),
 * VT (0x0B), FF (0x0C), and escape (0xFE). Manages column
 * tracking and tab expansion. Uses the spin lock for buffer access.
 *
 * Parameters:
 *   tty   - TTY descriptor
 *   buf   - Character buffer to output
 *   flags - Output flags (count in low 16 bits, mode in high 16 bits)
 *
 * Returns:
 *   Number of characters actually processed
 *
 * Original address: 0x00e1b00a
 */
uint16_t tty_$i_put_chars(tty_desc_t *tty, const uint8_t *buf, uint32_t flags);

/*
 * tty_$i_buf_put - Put a single byte into a TTY circular buffer
 *
 * Acquires the spin lock, inserts the byte into the circular buffer,
 * and releases the lock. Drops the byte if the buffer is full.
 *
 * Parameters:
 *   ch  - Character to insert (high byte of uint16_t on M68K stack)
 *   buf - Pointer to circular buffer header (head/tail indices)
 *
 * Original address: 0x00e1af42
 */
void tty_$i_buf_put(uint8_t ch, void *buf);

/*
 * tty_$i_buf_put_delay - Put a delay sequence into the output buffer
 *
 * Inserts a delay escape sequence (0xFE, 0x00, high, low) into
 * the output buffer. Also decrements the available count by 4.
 * Accesses parent frame's locals (Pascal nested procedure pattern).
 *
 * Parameters:
 *   delay_val - 16-bit delay value (split into high/low bytes)
 *
 * Original address: 0x00e1afa2
 */
void tty_$i_buf_put_delay(uint16_t delay_val);

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
