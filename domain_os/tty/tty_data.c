/*
 * tty/tty_data.c - TTY Subsystem Global Data Definitions
 *
 * Defines the global data structures for the TTY subsystem.
 * Original addresses:
 *   TTY_$SPIN_LOCK:               0xe2dd74
 *   PTR_TTY_$I_DXM_SIGNAL:        0xe1b8ac
 *   tty_$word_sep_bitmap:          0xe2ddb4
 *   DAT_00e2ddd4:                  0xe2ddd4
 *   DAT_00e2ddd8:                  0xe2ddd8
 *   status_$t_00e1bcf8:            0xe1bcf8
 */

#include "tty/tty_internal.h"

/*
 * TTY_$SPIN_LOCK - Spin lock for TTY operations
 *
 * Used to protect critical sections in TTY code.
 * Zero-initialized.
 *
 * Original address: 0xe2dd74
 */
uint32_t TTY_$SPIN_LOCK = 0;

/*
 * PTR_TTY_$I_DXM_SIGNAL - Function pointer to TTY_$I_DXM_SIGNAL
 *
 * Used by DXM_$ADD_CALLBACK for signal delivery.
 *
 * Original address: 0xe1b8ac
 */
m68k_ptr_t PTR_TTY_$I_DXM_SIGNAL = (m68k_ptr_t)TTY_$I_DXM_SIGNAL;

/*
 * tty_$word_sep_bitmap - Word separator character bitmap
 *
 * Bitmap used by TTY_$I_WORD_ERASE to classify characters as word
 * separators. Indexed as: byte[(0xFF - ch) >> 3], bit[ch & 7].
 * This is the A5-relative data base for the TTY module.
 * Initialized at runtime during TTY setup.
 *
 * Original address: 0xe2ddb4
 * Size: 32 bytes (covers 256 character positions)
 */
uint8_t tty_$word_sep_bitmap[32] = { 0 };

/*
 * DAT_00e2ddd4 - Output flags mask for raw mode
 *
 * Bitmask of output flag bits saved/cleared on raw mode entry.
 * Original address: 0xe2ddd4
 */
uint32_t DAT_00e2ddd4 = 0x0000001F;

/*
 * DAT_00e2ddd8 - Input flags mask for raw mode
 *
 * Bitmask of input flag bits saved/cleared on raw mode entry.
 * Original address: 0xe2ddd8
 */
uint32_t DAT_00e2ddd8 = 0x0000003C;

/*
 * status_$t_00e1bcf8 - Error status for crash character handling
 *
 * Status code passed to CRASH_SYSTEM when a crash character is received.
 * Original address: 0xe1bcf8
 * TODO(source-qvt): Determine the actual status code value.
 */
status_$t status_$t_00e1bcf8 = 0x000B0008;
