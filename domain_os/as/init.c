/*
 * AS_$INIT - Relocate the address-space layout for a 68020 machine
 * Original address: 0x00e3151c (90 bytes)
 *
 * Re-emitted from the disassembly.  Called once from OS_$INIT
 * (0x00E33844).  AS_$INFO is 0xE2B914; M68020 is the boolean byte at
 * 0xE23D2E (`tst.b / bpl': only the sign bit matters).
 *
 * 0x00E3151C  link.w A6,-0x8
 * 0x00E31520  tst.b M68020 / bpl 0x00E31572          68010: keep the defaults
 * 0x00E31528  A0 = &AS_$INFO
 * 0x00E3152E  (0x4,A0) = 0x033C0000                   global_a
 * 0x00E31536  (0x8,A0) = 0x00700000                   global_a_size
 * 0x00E3153E  A1 = A0 + 4; (0xC,A0) = (A1)+; (0x10,A0) = (A1)+
 *                                                     the 68020 copies
 * 0x00E3154A  D0 = 0x02A00000
 * 0x00E31550  (0x18,A0) += D0                         stack_file_low
 * 0x00E31554  (0x1C,A0) += D0                         cr_rec
 * 0x00E31558  (0x24,A0) += D0                         cr_rec_end
 * 0x00E3155C  (0x2C,A0) += D0                         stack_file_high
 * 0x00E31560  (0x34,A0) += D0                         stack_low
 * 0x00E31564  (0x3C,A0) += D0                         stack_high
 * 0x00E31568  (0x44,A0) += D0                         stack_offset
 * 0x00E3156C  (0x50,A0) = (0x24,A0)                   cr_rec_file = cr_rec_end
 * 0x00E31572  unlk / rts
 */

#include "as/as_internal.h"

void AS_$INIT(void)
{
    /*
     * 0x00E31520: tst.b (0x00e23d2e).l / bpl.  The byte tested is the HIGH
     * (first, big-endian) byte of the M68020 word, so its sign is the sign
     * of the whole word; `(int8_t)M68020' would read the low byte at
     * 0xE23D2F on the target.
     */
    if ((int16_t)M68020 < 0) {
        /* 0x00E3152E / 0x00E31536 */
        AS_$INFO.global_a = M68020_GLOBAL_A_BASE;
        AS_$INFO.global_a_size = M68020_GLOBAL_A_SIZE;

        /* 0x00E3153E..0x00E31546 */
        AS_$INFO.m68020_global_a = AS_$INFO.global_a;
        AS_$INFO.m68020_global_a_size = AS_$INFO.global_a_size;

        /* 0x00E3154A..0x00E31568 */
        AS_$INFO.stack_file_low += M68020_AS_OFFSET;
        AS_$INFO.cr_rec += M68020_AS_OFFSET;
        AS_$INFO.cr_rec_end += M68020_AS_OFFSET;
        AS_$INFO.stack_file_high += M68020_AS_OFFSET;
        AS_$INFO.stack_low += M68020_AS_OFFSET;
        AS_$INFO.stack_high += M68020_AS_OFFSET;
        AS_$INFO.stack_offset += M68020_AS_OFFSET;

        /* 0x00E3156C */
        AS_$INFO.cr_rec_file = AS_$INFO.cr_rec_end;
    }
}
