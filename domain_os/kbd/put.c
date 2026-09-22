/*
 * KBD_$PUT - "Write" to a keyboard line
 *
 * Looks the descriptor up and, if that succeeded, calls the module-local
 * procedure at 0x00E1CA8A with (desc, *type, str, *length).  That procedure
 * is a single `rts` (the bytes at 0x00E1CA86 are `unlk A6 / rts` closing
 * kbd_$get_mode, then `rts` at 0x00E1CA8A, then kbd_$set_type's `link` at
 * 0x00E1CA8C): an empty Pascal procedure.  KBD_$PUT therefore has no effect
 * beyond KBD_$GET_DESC's status and side effects.
 *
 * Parameters (frame 0x00E1CEB4..0x00E1CED6):
 *   0x08 line_ptr - by reference, passed on to KBD_$GET_DESC
 *   0x0C type_ptr - word by reference; its VALUE is pushed
 *   0x10 str      - passed as a pointer
 *   0x14 length   - word by reference; its VALUE is pushed
 *   0x18 status   - status return (A3)
 *
 * Original address: 0x00e1ceac, 64 bytes
 *
 *   00e1ceb8  pea (A3) / move.l (0x8,A6) / jsr KBD_$GET_DESC -> A2
 *   00e1cec8  tst.l (A3) / bne -> exit
 *   00e1cecc  move.w (*length),-(SP) / move.l str,-(SP) / move.w (*type),-(SP) / pea (A2)
 *   00e1cede  bsr.w 0x00e1ca8a                     ; -> rts; args reclaimed by unlk
 */

#include "kbd/kbd_internal.h"

/*
 * The empty procedure at 0x00E1CA8A.  No link frame, no result slot; the
 * four arguments are simply ignored.
 */
static void kbd_$put_line(kbd_state_t *desc, uint16_t type, void *str,
                          uint16_t length)
{
    (void)desc;
    (void)type;
    (void)str;
    (void)length;
    /* 0x00E1CA8A: rts */
}

void KBD_$PUT(uint16_t *line_ptr, uint16_t *type_ptr, void *str,
              uint16_t *length, status_$t *status)
{
    kbd_state_t *desc;      /* A2 */

    /* 0x00E1CEB8..0x00E1CEC6 */
    desc = KBD_$GET_DESC(line_ptr, status);

    /* 0x00E1CEC8 */
    if (*status == status_$ok) {
        /* 0x00E1CECC..0x00E1CEDE */
        kbd_$put_line(desc, *type_ptr, str, *length);
    }
}
