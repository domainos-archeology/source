/*
 * AS_$GET_ADDR - Base and size of one address-space region
 * Original address: 0x00e583f0 (144 bytes; Ghidra's 152 includes the
 *                   8-byte jump table at 0x00E5840E)
 *
 * Re-emitted from the disassembly.  Reached through the SVC table
 * (0x00E7B5FA).  The region cells are the MST_ segment numbers, each a
 * 32KB segment count shifted left by 15 (`lsl.l #8 / lsl.l #7').
 *
 * Frame: (0x8,A6) addr_range (base, size longwords), (0xC,A6) region
 * (pointer to a word).
 *
 * 0x00E583F0  link.w A6,0x0; A0 = addr_range; A1 = region; D0 = *region
 * 0x00E583FE  cmpi.w #4,D0 / bcc 0x00E5847A            unsigned: 0..3 only
 * 0x00E58404  jump table at 0x00E5840E (bytes 0008 001c 0034 004c):
 *   0 -> 0x00E58416  base = 0; size = MST_$PRIVATE_A_SIZE (0xE2445C) << 15
 *   1 -> 0x00E5842A  base = MST_$SEG_GLOBAL_A (0xE24460) << 15;
 *                    D1 = MST_$GLOBAL_A_SIZE (0xE24462); bra 0x00E58470
 *   2 -> 0x00E58442  base = MST_$SEG_PRIVATE_B (0xE24458) << 15;
 *                    size = 0x40000
 *   3 -> 0x00E5845A  base = MST_$SEG_GLOBAL_B (0xE24452) << 15;
 *                    D1 = MST_$GLOBAL_B_SIZE (0xE2444A)
 * 0x00E58470  size = D1 << 15                          shared by 1 and 3
 * 0x00E5847A  base = 0x7FFFFFFF; size = 0              anything else
 * 0x00E58484  unlk / rts
 */

#include "as/as_internal.h"

/* 0x00E58450: private B is a fixed 8 segments */
#define AS_PRIVATE_B_SIZE   0x40000u

/* 0x00E5847A: the "no such region" base */
#define AS_NO_REGION_BASE   0x7FFFFFFFu

void AS_$GET_ADDR(as_$addr_range_t *addr_range, int16_t *region)
{
    uint16_t which = (uint16_t)*region;     /* D0.w, compared unsigned */

    /* 0x00E583FE / 0x00E58406 */
    switch (which) {
    case AS_REGION_PRIVATE_A:
        /* 0x00E58416..0x00E58428 */
        addr_range->base = 0;
        addr_range->size = (uint32_t)MST_$PRIVATE_A_SIZE << SEGMENT_SHIFT;
        break;

    case AS_REGION_GLOBAL_A:
        /* 0x00E5842A..0x00E58440, then 0x00E58470 */
        addr_range->base = (uint32_t)MST_$SEG_GLOBAL_A << SEGMENT_SHIFT;
        addr_range->size = (uint32_t)MST_$GLOBAL_A_SIZE << SEGMENT_SHIFT;
        break;

    case AS_REGION_PRIVATE_B:
        /* 0x00E58442..0x00E58458 */
        addr_range->base = (uint32_t)MST_$SEG_PRIVATE_B << SEGMENT_SHIFT;
        addr_range->size = AS_PRIVATE_B_SIZE;
        break;

    case AS_REGION_GLOBAL_B:
        /* 0x00E5845A..0x00E5846E, then 0x00E58470 */
        addr_range->base = (uint32_t)MST_$SEG_GLOBAL_B << SEGMENT_SHIFT;
        addr_range->size = (uint32_t)MST_$GLOBAL_B_SIZE << SEGMENT_SHIFT;
        break;

    default:
        /* 0x00E5847A / 0x00E58480 */
        addr_range->base = AS_NO_REGION_BASE;
        addr_range->size = 0;
        break;
    }
}
