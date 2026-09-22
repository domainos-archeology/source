/*
 * TERM_$READ - Read from a terminal line
 *
 * Maps the line, picks the blocking or conditional TTY_$K_GET option from
 * the DTTE's flags byte (bit 7 set = conditional), calls TTY_$K_GET and
 * converts the status.
 *
 * Parameters (frame 0x00E66824..0x00E66830):
 *   0x08 line_ptr   - word by reference (A3); passed on to GET_REAL_LINE
 *                     (by value) and TTY_$K_GET (by reference)
 *   0x0C buffer     - passed on (D2)
 *   0x10 count      - passed on (A4)
 *   0x14 status_ret - status return (A2)
 *
 * Returns (D0w): TTY_$K_GET's result.
 *
 * Original address: 0x00e6681c, 122 bytes
 *
 *   00e66834  subq.l #2 / pea (A2) / move.w (A3) / jsr TERM_$GET_REAL_LINE -> D0
 *   00e66842  tst.l (A2) / bne -> 0x00E6688A
 *   00e66846  D1 = line*0x38; tst.b (0x36,0xe2dc90,D1) / bpl -> blocking
 *   00e6685e  pea (A2) / pea (A4) / move.l D2 / pea (0x30,PC) -> 0xE66896 (word 1)
 *   00e6686a  pea (A2) / pea (A4) / move.l D2 / pea (0x26,PC) -> 0xE66898 (word 0)
 *   00e66874  pea (A3) / jsr TTY_$K_GET / lea (0x14,SP),SP -> D2w
 *   00e66882  pea (A2) / jsr 0x00e1aaa8 (TERM_$STATUS_CONVERT)   ; args reclaimed by unlk
 *   00e6688a  move.w D2w,D0w
 *
 * QUIRK: on the GET_REAL_LINE error exit D2 still holds the `buffer`
 * argument, so the function returns the low word of that pointer.  The C
 * reproduces it with the pointer's 32-bit VA.
 */

#include "term/term_internal.h"

/* 0x00E66896 / 0x00E66898, `gsk read 0xE66896 4`: 00 01 00 00 */
const uint16_t term_$const_word_1 = 1;
const uint16_t term_$const_word_0 = 0;

unsigned short TERM_$READ(short *line_ptr, void *buffer, void *count,
                          status_$t *status_ret)
{
    int16_t line;                   /* D0w */
    const uint16_t *options;
    uint16_t result;                /* D2w */

    /* 0x00E66834..0x00E66840 */
    line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);

    /* 0x00E66842 */
    if (*status_ret != status_$ok) {
        /* 0x00E6688A: D2 is still the buffer argument */
        return (unsigned short)ARCH_PTR_TO_VA(buffer);
    }

    /* 0x00E66846..0x00E66870: a Domain boolean test on dtte[line].flags */
    if ((int8_t)DTTE[(int16_t)line].flags < 0) {
        options = &term_$const_word_1;
    } else {
        options = &term_$const_word_0;
    }

    /* 0x00E66874..0x00E66880 */
    result = TTY_$K_GET(line_ptr, (void *)options, buffer, (ushort *)count,
                        status_ret);

    /* 0x00E66882..0x00E66884 */
    TERM_$STATUS_CONVERT(status_ret);

    /* 0x00E6688A */
    return result;
}
