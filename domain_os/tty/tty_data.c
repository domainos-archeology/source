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
DXM_$DEFINE_CALLBACK_CELL(PTR_TTY_$I_DXM_SIGNAL, TTY_$I_DXM_SIGNAL);

/*
 * tty_$word_sep_bitmap - Word separator character bitmap
 *
 * Bitmap used by TTY_$I_WORD_ERASE to classify characters as word
 * separators. Indexed as: byte[(0xFF - ch) >> 3], bit[ch & 7].
 * This is the A5-relative data base for the TTY module (TTY_$I_RCV sets
 * A5 = 0xe2ddb4 at 0x00e1b932 and TTY_$I_WORD_ERASE inherits it).
 *
 * The image initializes it statically; `gsk read 0xe2ddb4 32` gives
 *
 *   00e2ddb4  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *   00e2ddc4  00 00 00 00 00 00 00 00  00 00 00 01 00 00 37 00
 *
 * i.e. byte[0x1b] = 0x01 and byte[0x1e] = 0x37.  Decoding those with
 * ch = 0xff - (idx * 8 + bit):
 *
 *   byte[0x1b] bit 0        -> 0x20  space
 *   byte[0x1e] bits 0,1,2   -> 0x08  BS, 0x09 HT, 0x0a LF
 *   byte[0x1e] bits 4,5     -> 0x0c  FF, 0x0d CR
 *
 * so the separator set is { BS, HT, LF, FF, CR, space }.  Nothing in the
 * image writes the table (the only references to 0xe2ddb4 are the six
 * `lea (0xe2ddb4).l,A5` module-base loads in tty/).
 *
 * Original address: 0xe2ddb4
 * Size: 32 bytes (covers 256 character positions)
 */
uint8_t tty_$word_sep_bitmap[32] = {
    [0x1b] = 0x01,      /* space */
    [0x1e] = 0x37,      /* BS, HT, LF, FF, CR */
};

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
