/*
 * KBD_$INQ_KBD_TYPE - Return a line's keyboard type string
 *
 * Copies the descriptor's kbd_type_len bytes of kbd_type_str into the
 * caller's buffer and stores the length.
 *
 * Parameters (frame 0x00E7256A..0x00E7259C):
 *   0x08 line_ptr - by reference, passed on to KBD_$GET_DESC
 *   0x0C type_buf - receives the string bytes (A2)
 *   0x10 type_len - receives the length word
 *   0x14 status   - status return (A3)
 *
 * Original address: 0x00e72562, 76 bytes
 *
 *   00e72572  pea (A3) / move.l (0x8,A6) / jsr KBD_$GET_DESC -> A0
 *   00e72580  tst.l (A3) / bne -> exit
 *   00e72584  move.w (0x44,A0),D0w / subq.w #1 / bmi -> skip copy
 *   00e7258c  D1 = len-1; D0 = 0; loop: (A2,D0) = (0x40,A0,D0); D0++; dbf D1
 *   00e7259c  movea.l (0x10,A6),A1 / move.w (0x44,A0),(A1)
 */

#include "kbd/kbd_internal.h"

void KBD_$INQ_KBD_TYPE(uint16_t *line_ptr, uint8_t *type_buf,
                       uint16_t *type_len, status_$t *status)
{
    kbd_state_t *desc;      /* A0 */
    int16_t count;          /* D1w */
    uint16_t i;             /* D0w */

    /* 0x00E72572..0x00E7257E */
    desc = KBD_$GET_DESC(line_ptr, status);

    /* 0x00E72580 */
    if (*status == status_$ok) {
        /* 0x00E72584..0x00E72598: dbf over kbd_type_len bytes (none if 0) */
        count = (int16_t)(desc->kbd_type_len - 1);
        if (count >= 0) {
            for (i = 0; count >= 0; count--, i++) {
                type_buf[i] = desc->kbd_type_str[i];
            }
        }

        /* 0x00E7259C..0x00E725A0 */
        *type_len = desc->kbd_type_len;
    }
}
