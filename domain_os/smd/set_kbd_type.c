/*
 * smd/set_kbd_type.c - SMD_$SET_KBD_TYPE implementation
 *
 * Sets the keyboard type.
 *
 * Original address: 0x00E6E1AE
 *
 * Assembly:
 *   link.w A6,0x0
 *   move.l (0x10,A6),-(SP)   ; push status_ret
 *   move.l (0xc,A6),-(SP)    ; push length
 *   move.l (0x8,A6),-(SP)    ; push kbd_type
 *   pea (-0x894,PC)          ; push &SMD_ACQ_LOCK_DATA (0x00E6D92C)
 *   jsr KBD_$SET_KBD_TYPE
 *   unlk A6
 *   rts
 *
 * This is a simple wrapper around KBD_$SET_KBD_TYPE.
 */

#include "smd/smd_internal.h"
#include "kbd/kbd.h"

/* The "line" argument passed to KBD_$* is the constant word at 0x00E6D92C,
 * SMD_ACQ_LOCK_DATA (declared in smd/smd_internal.h). */

/*
 * SMD_$SET_KBD_TYPE - Set keyboard type
 *
 * Sets the keyboard type for the display.
 * Wrapper around KBD_$SET_KBD_TYPE.
 *
 * Parameters:
 *   kbd_type   - Keyboard type string
 *   length     - Length of type string
 *   status_ret - Output: status return
 */
void SMD_$SET_KBD_TYPE(uint8_t *kbd_type, uint16_t *length, status_$t *status_ret)
{
    KBD_$SET_KBD_TYPE(&SMD_ACQ_LOCK_DATA, kbd_type, length, status_ret);
}
