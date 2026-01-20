/*
 * XNS IDP Checksum Functions
 *
 * Implementation of the XNS IDP checksum algorithm. The XNS checksum
 * uses one's complement addition with end-around carry, followed by
 * a left rotation of the result after each word is added.
 *
 * Original addresses:
 *   XNS_IDP_$CHECKSUM:   0x00E2B850
 *   XNS_IDP_$HOP_AND_SUM: 0x00E2B872
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$CHECKSUM - Calculate IDP checksum
 *
 * Computes the XNS IDP checksum using the following algorithm:
 *   1. Initialize sum to 0
 *   2. For each 16-bit word in the data:
 *      a. Add word to sum using one's complement addition (add carry back)
 *      b. Rotate sum left by 1 bit
 *   3. If result is 0xFFFF, return 0 (0xFFFF means "no checksum")
 *
 * Assembly analysis (0x00E2B850):
 *   moveq #0x0,D0              ; sum = 0
 *   movea.l (0x4,SP),A0        ; A0 = data pointer
 *   move.w (0x8,SP),D1w        ; D1 = word_count
 *   subq.w #0x1,D1w            ; D1 = word_count - 1 (for dbf)
 * loop:
 *   add.w (A0)+,D0w            ; sum += *data++
 *   bcc.b skip                 ; if no carry, skip
 *   addq.w #0x1,D0w            ; sum += 1 (end-around carry)
 * skip:
 *   rol.w #0x1,D0w             ; sum = rotate_left(sum, 1)
 *   dbf D1w,loop               ; loop while D1 >= 0
 *   cmp.w #-0x1,D0w            ; if sum == 0xFFFF
 *   bne.b done
 *   moveq #0x0,D0              ; sum = 0
 * done:
 *   rts
 *
 * @param data          Pointer to data (must be word-aligned)
 * @param word_count    Number of 16-bit words to checksum
 *
 * @return Checksum value, or 0 if computed checksum is 0xFFFF
 */
uint16_t XNS_IDP_$CHECKSUM(uint16_t *data, int16_t word_count)
{
    uint16_t sum = 0;
    int16_t count = word_count - 1;

    do {
        uint16_t word = *data++;
        uint16_t new_sum = sum + word;

        /* Handle one's complement carry (end-around carry) */
        if (new_sum < sum) {
            new_sum++;
        }

        /* Rotate left by 1 bit */
        sum = (new_sum << 1) | (new_sum >> 15);
        count--;
    } while (count >= 0);

    /* 0xFFFF is reserved to mean "no checksum", so return 0 instead */
    if (sum == 0xFFFF) {
        sum = 0;
    }

    return sum;
}

/*
 * XNS_IDP_$HOP_AND_SUM - Calculate hop count contribution to checksum
 *
 * When forwarding an IDP packet, the hop count is incremented. This
 * function computes the checksum adjustment needed to account for the
 * hop count change without recomputing the entire checksum.
 *
 * The algorithm:
 *   1. Calculate rotation count based on hop offset position in packet
 *      rotation = ((hop_offset - 3) >> 1) & 0x0F
 *   2. Compute 0x100 rotated left by that amount
 *   3. Add to current sum with end-around carry
 *   4. Handle 0xFFFF -> 0 conversion
 *
 * Assembly analysis (0x00E2B872):
 *   move.w (0x6,SP),D1w        ; D1 = hop_offset
 *   subq.w #0x3,D1w            ; D1 = hop_offset - 3
 *   asr.w #0x1,D1w             ; D1 = (hop_offset - 3) / 2
 *   and.w #0xf,D1w             ; D1 = D1 & 0x0F
 *   move.w #0x100,D0w          ; D0 = 0x100
 *   tst.w D1w                  ; if D1 == 0
 *   beq.b skip_rot             ; skip rotation
 *   rol.w D1,D0w               ; D0 = rotate_left(0x100, D1)
 * skip_rot:
 *   add.w (0x4,SP),D0w         ; D0 = D0 + current_sum
 *   bcc.b no_carry             ; if no carry, skip
 *   addq.w #0x1,D0w            ; D0 += 1 (end-around carry)
 * no_carry:
 *   cmp.w #-0x1,D0w            ; if D0 == 0xFFFF
 *   bne.b done
 *   moveq #0x0,D0              ; D0 = 0
 * done:
 *   rts
 *
 * @param current_sum   Current checksum value
 * @param hop_offset    Offset to hop count field (typically 5 for transport_ctl)
 *
 * @return Updated checksum value
 */
int16_t XNS_IDP_$HOP_AND_SUM(uint16_t current_sum, int16_t hop_offset)
{
    /* Calculate rotation count from hop offset */
    uint16_t rotation = ((hop_offset - 3) >> 1) & 0x0F;

    /* Start with 0x100 (the increment to hop count) */
    uint16_t contribution = 0x100;

    /* Rotate left by the computed amount (unless zero) */
    if (rotation != 0) {
        contribution = (contribution << rotation) | (contribution >> (16 - rotation));
    }

    /* Add to current sum with end-around carry */
    int16_t new_sum = (int16_t)(current_sum + contribution);
    if ((uint16_t)new_sum < current_sum) {
        new_sum++;
    }

    /* 0xFFFF -> 0 conversion */
    if (new_sum == -1) {
        new_sum = 0;
    }

    return new_sum;
}
