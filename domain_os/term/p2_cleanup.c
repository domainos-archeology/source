/*
 * TERM_$P2_CLEANUP - Drop a dying process's ownership of terminal lines
 *
 * Walks the three per-line records inside TERM_$DATA (0x158, 0x634, 0xB10;
 * stride 0x4DC) and, wherever the owner UID at record + 0x4C equals
 * PROC2_$UID[*as_id], overwrites it with UID_$NIL.
 *
 * Parameters:
 *   as_id_ptr - word by reference ((0x8,A6)); the index into PROC2_$UID
 *
 * Original address: 0x00e751f0, 96 bytes
 *
 *   00e751f8  moveq #2,D0                                ; dbf -> 3 records
 *   00e751fa  D2 = 0xE7BE94 (PROC2_$UID); D3 = 0xE1737C (UID_$NIL)
 *   00e7520a  D1 = *as_id << 3                           ; sign-extended word index
 *   00e7520c  A2 = 0xE2C9F0 + 0x4DC; A1 = A0 = A2
 *   00e7521c  loop: A2 = D2 + D1; A3 = A1 - 0x338        ; record uid = base + 0x1A4 + i*0x4DC
 *   00e75226  moveq #1,D4                                ; never used
 *   00e75228  cmpm.l (A3)+,(A2)+ / bne; cmpm.l / bne     ; 8-byte compare
 *   00e75230  A2 = D3; (-0x338,A0) = nil.high; (-0x334,A0) = nil.low
 *   00e7523a  A0 += 0x4DC; A1 += 0x4DC; dbf D0
 */

#include "term/term_internal.h"

#define TERM_LINE_RECORD_STRIDE   0x4DC
#define TERM_LINE_OWNER_UID       0x1A4     /* TERM_$DATA + 0x158 + 0x4C */

void TERM_$P2_CLEANUP(short *as_id_ptr)
{
    int16_t count;          /* D0w */
    int16_t uid_offset;     /* D1w */
    uid_t *proc_uid;        /* A2 at 0x00E7521C */
    uid_t *owner;           /* A3 */
    int i;

    /* 0x00E7520A..0x00E75212 */
    uid_offset = (int16_t)(*as_id_ptr << 3);
    proc_uid = (uid_t *)((uint8_t *)PROC2_$UID + uid_offset);

    /* 0x00E7521C..0x00E75242 */
    for (count = 2, i = 0; count >= 0; count--, i++) {
        owner = (uid_t *)TERM_$DATA_AT(TERM_LINE_OWNER_UID + i * TERM_LINE_RECORD_STRIDE);
        if (owner->high == proc_uid->high && owner->low == proc_uid->low) {
            /* 0x00E75230..0x00E75236 */
            owner->high = UID_$NIL.high;
            owner->low = UID_$NIL.low;
        }
    }
}
