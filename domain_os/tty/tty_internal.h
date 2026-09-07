/*
 * tty/tty_internal.h - Internal TTY Definitions
 *
 * Contains internal functions and data used only within
 * the TTY subsystem. External consumers should use tty/tty.h.
 */

#ifndef TTY_INTERNAL_H
#define TTY_INTERNAL_H

#include "tty/tty.h"
#include "dxm/dxm.h"
#include "uid/uid.h"   /* UID_$NIL */

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
 * TTY_$I_STORE_PARITY - Store a character with optional parity marking
 *
 * Pascal nested procedure from TTY_$I_ERR. Stores either a 3-byte
 * parity error sequence (0xFF, 0x00, ch) or just 0x00, then checks
 * break mode for signaling.
 *
 * NOTE: In the original code this is a Pascal nested procedure that
 * accesses the parent frame (TTY_$I_ERR) directly. We flatten it
 * to take explicit parameters.
 *
 * Parameters:
 *   tty - TTY descriptor (from parent frame offset 0x08)
 *   ch  - Character received (from parent frame offset 0x0C)
 *
 * Original address: 0x00e1bcfc
 */
void TTY_$I_STORE_PARITY(tty_desc_t *tty, uint8_t ch);

/*
 * TTY_$I_SET_RAW_MODE - Switch TTY between raw and cooked modes
 *
 * Saves/restores flag bits and reconfigures the TTY for raw or
 * cooked (canonical) mode operation.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   raw - Raw mode flag (negative = enter raw, non-negative = leave raw)
 *
 * Original address: 0x00e1bf70
 */
void TTY_$I_SET_RAW_MODE(tty_desc_t *tty, char raw);

/*
 * TTY_$I_NEWLINE - Output a newline sequence
 *
 * Outputs CR/LF or LF depending on output flags.
 *
 * Parameters:
 *   tty - TTY descriptor
 *
 * Original address: 0x00e1b456
 */
void TTY_$I_NEWLINE(tty_desc_t *tty);

/*
 * TTY_$I_CALC_COLUMN - Calculate display column position
 *
 * Walks the input buffer tracking the display column, handling
 * TAB, BS, CR, control chars, and normal characters.
 *
 * Parameters:
 *   buf        - Pointer to circular buffer read position (&tty->input_read)
 *   start      - Starting position index (1-256)
 *   column     - Initial column value
 *   echo_flags - Echo flags (bit 4 = echo control chars as ^X)
 *
 * Returns:
 *   Calculated column position
 *
 * Original address: 0x00e1b4b6
 */
uint16_t TTY_$I_CALC_COLUMN(void *buf, int16_t start, uint16_t column, uint32_t echo_flags);

/*
 * TTY_$I_DELETE_CHAR - Delete the last character from input buffer
 *
 * Removes the most recently typed character and provides visual
 * feedback depending on the echo mode (CRT erase, echo erase, etc.).
 *
 * Parameters:
 *   tty - TTY descriptor
 *
 * Original address: 0x00e1b538
 */
void TTY_$I_DELETE_CHAR(tty_desc_t *tty);

/*
 * TTY_$I_KILL_LINE - Erase the entire input line
 *
 * Erases all pending input either by repeated delete-char (CRT mode)
 * or by echoing kill character and resetting buffer pointers.
 *
 * Parameters:
 *   tty - TTY descriptor
 *
 * Original address: 0x00e1b6ac
 */
void TTY_$I_KILL_LINE(tty_desc_t *tty);

/*
 * TTY_$I_WORD_ERASE - Erase the previous word from input
 *
 * Deletes backward: first skips word separators, then deletes
 * through the previous word. Uses a bitmap for classification.
 *
 * Parameters:
 *   tty - TTY descriptor
 *
 * Original address: 0x00e1b716
 */
void TTY_$I_WORD_ERASE(tty_desc_t *tty);

/*
 * TTY_$I_BREAK_CHAR - Process a break/newline character
 *
 * Handles line-terminating characters: stores in buffer, echoes,
 * advances head pointer, and wakes readers.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - The break/newline character
 *
 * Original address: 0x00e1b8b0
 */
void TTY_$I_BREAK_CHAR(tty_desc_t *tty, uint8_t ch);

/*
 * TTY_$I_PUT_OUTPUT - Write to output with input-pending check
 *
 * Wrapper around tty_$i_put_chars that defers output when
 * input is pending and the defer flag is set.
 *
 * Parameters:
 *   tty   - TTY descriptor
 *   buf   - Character buffer to output
 *   count - Number of characters
 *   max   - Maximum to process
 *
 * Returns:
 *   Number of characters written, or 0 if deferred
 *
 * Original address: 0x00e1bf0e
 */
int16_t TTY_$I_PUT_OUTPUT(tty_desc_t *tty, void *buf, uint16_t count, uint16_t max);

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
 * tty_$i_buf_insert - Insert byte into circular buffer (no lock)
 *
 * Low-level circular buffer insert without acquiring the spin lock.
 * Stores the byte at the current tail position and advances tail.
 * Wraps tail from 0x100 back to 1. Drops the byte if buffer is full
 * (tail+1 == head). Called by tty_$i_buf_put (which wraps with lock)
 * and directly by TTY_$I_RCV for certain character classes.
 *
 * Buffer layout: head at offset 0, tail at offset 2, data at offset 5.
 * Valid positions: 1..0x100 (256 entries).
 *
 * Parameters:
 *   ch  - Character to insert
 *   buf - Pointer to circular buffer header
 *
 * Original address: 0x00E1AF0A
 * Size: 56 bytes
 */
void tty_$i_buf_insert(uint8_t ch, void *buf);

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
 * tty_$i_wait - Wait for TTY input with timeout
 *
 * Eventcount-based wait for TTY input data or timeout.
 * Sets up EC_$WAITN with 2-3 eventcounts (TTY data, quit signal,
 * and optionally a timeout via TIME_$ADVANCE). Handles:
 *   - Quit signal: status 0x350007
 *   - Timeout with no data: status 0x350008
 *   - Data available: sets *done_flag = 0xFF
 * Releases/re-acquires TTY lock around the wait.
 *
 * Parameters:
 *   tty       - TTY descriptor
 *   wait_flag - Negative to require data (error on timeout)
 *   done_flag - Output: set to 0xFF when data ready
 *   count     - Characters to wait for
 *   status    - Output: status code
 *
 * Original address: 0x00E1C204
 * Size: 460 bytes
 */
void tty_$i_wait(tty_desc_t *tty, char wait_flag, char *done_flag,
                 uint16_t count, status_$t *status);

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
 * DAT_00e82450 - Default enabled function character mask
 *
 * Bitmask of default-enabled function characters.
 * Used by TTY_$I_SET_DFL_FUNCS.
 * Original address: 0x00e82450
 */
extern uint32_t DAT_00e82450;

/*
 * DAT_00e2ddd4 - Output flags mask for raw mode save/restore
 *
 * Bitmask of output flag bits that are cleared when entering raw mode
 * and restored when leaving raw mode. Value: 0x0000001F.
 * Original address: 0x00e2ddd4
 */
extern uint32_t DAT_00e2ddd4;

/*
 * DAT_00e2ddd8 - Input flags mask for raw mode save/restore
 *
 * Bitmask of input flag bits that are cleared when entering raw mode
 * and restored when leaving raw mode. Value: 0x0000003C.
 * Original address: 0x00e2ddd8
 */
extern uint32_t DAT_00e2ddd8;

/*
 * tty_$word_sep_bitmap - Word separator character bitmap
 *
 * Used by TTY_$I_WORD_ERASE to classify characters as word separators.
 * Indexed as: byte[(0xFF - ch) >> 3], bit[ch & 7].
 * Initialized at runtime during TTY setup.
 * Original address: 0x00e2ddb4 (A5 base for TTY module)
 */
extern uint8_t tty_$word_sep_bitmap[];

/*
 * PTR_TTY_$I_DXM_SIGNAL - cell holding TTY_$I_DXM_SIGNAL's address
 *
 * TTY_$I_SIGNAL pushes the ADDRESS of this cell to DXM_$ADD_CALLBACK, so the
 * cell holds the callback's 4-byte code address.  dxm_$callback_t keeps the
 * queue entry 16 bytes on every target (source-wy9y).
 */
extern dxm_$callback_t PTR_TTY_$I_DXM_SIGNAL;

#endif /* TTY_INTERNAL_H */
