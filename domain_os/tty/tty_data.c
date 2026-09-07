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

/*
 * ============================================================================
 * TTY module block at 0x00E8242C
 * ============================================================================
 *
 * The map segment is "D E8242C TTY size = 2C" (0x00E8242C..0x00E82458) and it
 * exports no interior symbol; six `lea (0xe8242c).l,A5` loads in tty/ make it
 * the module's A5 base.  Its first 0x24 bytes are the 18 default function
 * character classes (words 0007 0009 0008 000C 000B 0010 000F 000E 0001 0000
 * 0002 0003 0004 0005 0006 000D 000A 0000); the two longwords below finish it.
 *
 * TODO(source-wk2f, 0x00E8242C): the 18-word class table has no tree symbol
 * yet, so the block is not modelled as one record.
 */

/*
 * tty_$i_dfl_func_enable_mask - A5+0x24 = 0x00E82450.
 * Image longword: 0x0001FFFF (all 17 function characters enabled).
 */
uint32_t tty_$i_dfl_func_enable_mask = 0x0001FFFF;

/*
 * DAT_00e82454 - A5+0x28 = 0x00E82454.
 * Image longword: 0x000000D0, the mask TTY_$K_SET_INPUT_BREAK_MODE enables
 * or disables when it switches between raw and line mode.
 */
uint32_t DAT_00e82454 = 0x000000D0;

