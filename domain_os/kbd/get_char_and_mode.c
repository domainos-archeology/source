/*
 * KBD_$GET_CHAR_AND_MODE - Fetch one key and its translated mode
 *
 * Looks the line's keyboard descriptor up, pulls the next key out of its
 * ring with kbd_$fetch_key and maps the mode word that comes back through
 * KBD_$MODE_TABLE.  Returns kbd_$fetch_key's Domain boolean (0xFF = a key
 * was available) or 0 when the descriptor lookup failed.
 *
 * Parameters (frame 0x00E724CC..0x00E72502):
 *   0x08 line_ptr - terminal line number, by reference (passed on to GET_DESC)
 *   0x0C char_out - receives the key byte (passed on to kbd_$fetch_key)
 *   0x10 mode_out - receives KBD_$MODE_TABLE[mode]
 *   0x14 status   - status return (A3)
 *
 * Original address: 0x00e724c4, 82 bytes
 *
 *   00e724d0  clr.b D2b                                   ; result = false
 *   00e724d2  pea (A3) / move.l (0x8,A6) / jsr KBD_$GET_DESC -> A2
 *   00e724e2  tst.l (A3) / bne -> return D2
 *   00e724e6  pea (-0x6,A6) / move.l (0xc,A6) / pea (A2) / jsr kbd_$fetch_key
 *             ; args reclaimed by unlk
 *   00e724f6  move.b D0b,D2b
 *   00e724f8  move.w (-0x6,A6),D0w                        ; mode, a WORD index
 *   00e724fc  movea.l #0xe2dde4,A0                        ; KBD_$MODE_TABLE
 *   00e72506  move.b (0x0,A0,D0w*0x1),(A1)                ; *mode_out
 *   00e7250a  move.b D2b,D0b
 */

#include "kbd/kbd_internal.h"

int8_t KBD_$GET_CHAR_AND_MODE(uint16_t *line_ptr, uint8_t *char_out,
                               uint8_t *mode_out, status_$t *status)
{
    kbd_state_t *desc;      /* A2 */
    int8_t result;          /* D2b */
    int16_t mode;           /* A6-0x6 */

    /* 0x00E724D0 */
    result = 0;

    /* 0x00E724D2..0x00E724E0 */
    desc = KBD_$GET_DESC(line_ptr, status);

    /* 0x00E724E2 */
    if (*status == status_$ok) {
        /* 0x00E724E6..0x00E724F6 */
        result = kbd_$fetch_key(desc, char_out, &mode);

        /* 0x00E724F8..0x00E72506: sign-extended word index into the 8 bytes */
        *mode_out = KBD_$MODE_TABLE[mode];
    }

    /* 0x00E7250A */
    return result;
}
