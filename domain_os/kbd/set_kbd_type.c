/*
 * KBD_$SET_KBD_TYPE - Set a line's keyboard type string
 *
 * Looks the descriptor up and hands the caller's string and length to
 * kbd_$set_type (a Pascal function whose result slot is reserved and
 * discarded).
 *
 * Parameters (frame 0x00E7252C..0x00E72546):
 *   0x08 line_ptr - by reference, passed on to KBD_$GET_DESC
 *   0x0C type_ptr - the string
 *   0x10 type_len - word by reference; its VALUE is pushed
 *   0x14 status   - status return (A3)
 *
 * Original address: 0x00e72524, 62 bytes
 *
 *   00e72530  pea (A3) / move.l (0x8,A6) / jsr KBD_$GET_DESC -> A2
 *   00e72540  tst.l (A3) / bne -> exit
 *   00e72544  subq.l #2 / move.w (*type_len) / move.l type_ptr / pea (A2)
 *   00e72552  jsr kbd_$set_type (0x00e1ca8c)          ; args reclaimed by unlk
 */

#include "kbd/kbd_internal.h"

void KBD_$SET_KBD_TYPE(uint16_t *line_ptr, void *type_ptr,
                       uint16_t *type_len, status_$t *status)
{
    kbd_state_t *desc;      /* A2 */

    /* 0x00E72530..0x00E7253E */
    desc = KBD_$GET_DESC(line_ptr, status);

    /* 0x00E72540 */
    if (*status == status_$ok) {
        /* 0x00E72544..0x00E72552 */
        kbd_$set_type(desc, (uint8_t *)type_ptr, *type_len);
    }
}
