/*
 * xns/idp_checksum.c - XNS_IDP_$CHECKSUM / XNS_IDP_$HOP_AND_SUM, portable model
 *
 * Original addresses (map segment `D E2B850 XNS_IDP_ASM size = 4C`):
 *   XNS_IDP_$CHECKSUM:    0x00E2B850 (34 bytes)
 *   XNS_IDP_$HOP_AND_SUM: 0x00E2B872 (40 bytes)
 *
 * On m68k both routines are hand-written assembly - no link frame, the
 * arguments read straight off the stack at (4,SP)/(8,SP) and (4,SP)/(6,SP),
 * `rts` with the result in D0.w - and are emitted verbatim in
 * xns/sau2/idp_checksum.s (byte-identical to the image), so this file
 * compiles to nothing there.  On every other target it reproduces the same
 * arithmetic, and the host tests exercise it.
 *
 * XNS_IDP_$CHECKSUM (0x00E2B850-0x00E2B870):
 *   moveq #0,D0 / movea.l (4,SP),A0 / move.w (8,SP),D1 / subq.w #1,D1
 *   loop: add.w (A0)+,D0 / bcc +2 / addq.w #1,D0 / rol.w #1,D0 / dbf D1,loop
 *   cmp.w #-1,D0 / bne +2 / moveq #0,D0 / rts
 *
 * XNS_IDP_$HOP_AND_SUM (0x00E2B872-0x00E2B898):
 *   move.w (6,SP),D1 / subq.w #3,D1 / asr.w #1,D1 / and.w #0xf,D1
 *   move.w #0x100,D0 / tst.w D1 / beq +2 / rol.w D1,D0
 *   add.w (4,SP),D0 / bcc +2 / addq.w #1,D0
 *   cmp.w #-1,D0 / bne +2 / moveq #0,D0 / rts
 */

#include "xns/xns_internal.h"

#if !defined(ARCH_M68K)

/*
 * XNS_IDP_$CHECKSUM - Calculate the IDP checksum
 *
 * One's-complement sum with end-around carry, rotated left one bit after
 * every word.  `subq.w #1` + `dbf` runs word_count times (word_count == 0
 * runs 0x10000 times, as on the target).  0xFFFF ("no checksum") becomes 0.
 *
 * @param data          Pointer to the words to sum
 * @param word_count    Number of 16-bit words
 *
 * @return Checksum, in D0.w
 */
uint16_t (XNS_IDP_$CHECKSUM)(uint16_t *data, uint32_t word_count_slot)
{
    int16_t word_count = (int16_t)ARCH_PASCAL_SLOT_WORD(word_count_slot); /* 8(SP) */
    uint16_t sum = 0;                                /* moveq #0,D0        */
    uint16_t count = (uint16_t)(word_count - 1);     /* subq.w #1,D1       */

    do {
        uint32_t wide = (uint32_t)sum + *data++;     /* add.w (A0)+,D0     */
        sum = (uint16_t)wide;
        if (wide & 0x10000u) {                       /* bcc / addq.w #1    */
            sum++;
        }
        sum = (uint16_t)((sum << 1) | (sum >> 15));  /* rol.w #1,D0        */
    } while (count-- != 0);                          /* dbf D1             */

    if (sum == 0xFFFF) {                             /* cmp.w #-1 / moveq  */
        sum = 0;
    }
    return sum;
}

/*
 * XNS_IDP_$HOP_AND_SUM - Fold an incremented hop count into a checksum
 *
 * Adds 0x100 rotated left by ((hop_offset - 3) >> 1) & 0xF to current_sum
 * with end-around carry; 0xFFFF becomes 0.
 *
 * @param current_sum   The packet's current checksum
 * @param hop_offset    Word offset of the hop-count byte's word
 *
 * @return Updated checksum, in D0.w
 */
int16_t (XNS_IDP_$HOP_AND_SUM)(uint32_t sum_hop_slot)
{
    uint16_t current_sum = ARCH_PASCAL_SLOT_WORD(sum_hop_slot);           /* 4(SP) */
    int16_t hop_offset = (int16_t)ARCH_PASCAL_SLOT_WORD2(sum_hop_slot); /* 6(SP) */
    uint16_t rotation = (uint16_t)(((int16_t)(hop_offset - 3) >> 1) & 0x0F);
    uint16_t contribution = 0x100;                   /* move.w #0x100,D0   */
    uint32_t wide;
    uint16_t sum;

    if (rotation != 0) {                             /* tst.w / beq        */
        contribution = (uint16_t)((contribution << rotation)
                                  | (contribution >> (16 - rotation)));
    }

    wide = (uint32_t)contribution + current_sum;     /* add.w (4,SP),D0    */
    sum = (uint16_t)wide;
    if (wide & 0x10000u) {                           /* bcc / addq.w #1    */
        sum++;
    }

    if (sum == 0xFFFF) {                             /* cmp.w #-1 / moveq  */
        sum = 0;
    }
    return (int16_t)sum;
}

#endif /* !ARCH_M68K */
