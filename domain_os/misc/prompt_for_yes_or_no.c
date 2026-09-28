/*
 * misc/prompt_for_yes_or_no.c - prompt_for_yes_or_no
 *
 * Original address: 0x00E33778 (module-local, no `$`; the SAU2 map has no
 * symbol for it - it sits inside the OS_ code segment)
 * Size: 88 bytes (0x00E33778 .. 0x00E337CF); its three constant cells
 * follow at 0x00E337D0.
 *
 * Reads a line from terminal line 1 and answers true for 'Y'/'y', false
 * for 'N'/'n'; anything else prints the "Please answer" message through
 * OS_$PRINT_INIT_ERROR and reads again.
 *
 * Frame (link.w A6,-0x14):
 *   (-0x8,A6)   status   status_$t TERM_$READ fills in (never tested)
 *   (-0x10,A6)  buffer   the 6-byte read buffer; only byte 0 is examined
 *
 *   0x00E3377C  pea (-0x8,A6)                 &status
 *   0x00E33780  pea (0x4e,PC)  -> 0x00E337D0  &max_len   (00 06)
 *   0x00E33784  pea (-0x10,A6)                buffer
 *   0x00E33788  pea (0x48,PC)  -> 0x00E337D2  &line      (00 01)
 *   0x00E3378C  jsr TERM_$READ; lea (0x10,SP),SP
 *   0x00E33796  clr.w D0w / move.b (-0x10,A6),D0b   the first byte, zero-extended
 *   0x00E3379C  cmpi.w #0x59 / #0x79 -> st D0b       'Y' / 'y' -> true
 *   0x00E337A8  cmpi.w #0x4e / #0x6e -> clr.b D0b    'N' / 'n' -> false
 *   0x00E337BE  pea (0x14,PC)  -> 0x00E337D4  the message; jsr OS_$PRINT_INIT_ERROR
 *   0x00E337CA  bra.b 0x00e3377c                     and read again
 *
 * Re-emitted from the disassembly 2026-09-27: the previous C printed the
 * message through VFMT_$WRITE10 and passed stack copies of the two word
 * cells.
 */

#include "misc/misc_internal.h"
#include "term/term.h"
#include "os/os_internal.h"

/* 0x00E337D0: 00 06 - the maximum length handed to TERM_$READ */
static const short prompt_max_len_00e337d0 = 6;
/* 0x00E337D2: 00 01 - terminal line 1 */
static const short prompt_line_00e337d2 = 1;
/* 0x00E337D4: the message, '%$'-terminated for the VFMT formatter */
static const char prompt_message_00e337d4[] = "Please answer \"yes\" or \"no\": %$";

/*
 * Returns the Domain boolean in D0b: 0xFF for yes, 0x00 for no.
 */
uint8_t prompt_for_yes_or_no(void)
{
    char buffer[8];             /* (-0x10,A6) */
    status_$t status;           /* (-0x8,A6) */
    uint16_t c;                 /* D0w */

    for (;;) {
        /* 0x00E3377C .. 0x00E33792 */
        TERM_$READ((short *)&prompt_line_00e337d2, buffer,
                   (void *)&prompt_max_len_00e337d0, &status);

        /* 0x00E33796 .. 0x00E337B4 */
        c = (uint8_t)buffer[0];
        if (c == 'Y' || c == 'y') {
            return 0xff;                                 /* 0x00E337B6 st D0b */
        }
        if (c == 'N' || c == 'n') {
            return 0x00;                                 /* 0x00E337BA clr.b D0b */
        }

        /* 0x00E337BE .. 0x00E337CA */
        OS_$PRINT_INIT_ERROR(prompt_message_00e337d4);
    }
}
