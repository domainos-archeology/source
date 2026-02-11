/*
 * TTY_$I_SET_DFL_FUNCS - Set default function character classes
 *
 * Wrapper that passes the default enabled function mask to
 * tty_$i_set_funcs. The default mask is stored in the TTY
 * global data area at 0xE82450 (offset 0x24 from base 0xE8242C).
 *
 * tty_$i_set_funcs iterates over all 18 function character slots
 * and updates the character class table (char_class[]) in the TTY
 * descriptor. For each function in the mask:
 *   - If use_dfl is true and the function is in the custom mask
 *     (tty->func_enabled at offset 0x20), set char_class to the
 *     default class from the global table
 *   - Otherwise, set char_class to NORMAL (0x12)
 *
 * Parameters:
 *   tty     - TTY descriptor
 *   use_dfl - If negative (true), use default class values from
 *             the global signal table; otherwise set all to NORMAL
 *
 * Original address: 0x00e6726e
 * Size: 36 bytes
 */

#include "tty/tty_internal.h"

/*
 * DAT_00e82450 - Default enabled function character mask
 *
 * Located at offset 0x24 from the TTY global data base (0xE8242C).
 * This is a bitmask indicating which function characters should be
 * active by default.
 */
extern uint32_t DAT_00e82450;

void TTY_$I_SET_DFL_FUNCS(tty_desc_t *tty, char use_dfl)
{
    tty_$i_set_funcs(tty, DAT_00e82450, use_dfl);
}
