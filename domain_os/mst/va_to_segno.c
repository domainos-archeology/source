/*
 * MST_$VA_TO_SEGNO - Convert a virtual address to an MST index
 *
 * Original address: 0x00E0DCC8 (SAU2 map: MST_WIRED, MST_$VA_TO_SEGNO at
 * E0DCC8)
 * Size: 120 bytes (0x00E0DCC8 .. 0x00E0DD3F)
 *
 * The virtual segment number (va >> 15) is classified into one of the four
 * regions and turned into the index of its MST word:
 *   private A   0 .. PRIVATE_A_SIZE-1            -> segno
 *   private B   SEG_PRIVATE_B .. +7              -> PRIVATE_A_SIZE + (segno - SEG_PRIVATE_B)
 *   global A    SEG_GLOBAL_A .. +GLOBAL_A_SIZE-1 -> segno - SEG_GLOBAL_A
 *   global B    SEG_GLOBAL_B ..                  -> GLOBAL_A_SIZE + (segno - SEG_GLOBAL_B)
 *
 * The result in D0w is default_result for a private segment, 0 for a
 * global one (and for a segment at or above SEG_MEM_TOP, for which
 * *segno_out is left untouched) and 0x3a for the gap between global A and
 * global B (also leaving *segno_out untouched).
 *
 * A5 = 0xE24304 (`lea (0xe24304).l,A5` at 0x00E0DCD0), the MST_WIRED data
 * segment; the cells read are (0x148,A5) MST_$SEG_MEM_TOP 0xE2444C,
 * (0x158,A5) MST_$PRIVATE_A_SIZE 0xE2445C, (0x154,A5) MST_$SEG_PRIVATE_B
 * 0xE24458, (0x15c,A5) MST_$SEG_GLOBAL_A 0xE24460, (0x15e,A5)
 * MST_$GLOBAL_A_SIZE 0xE24462 and (0x14e,A5) MST_$SEG_GLOBAL_B 0xE24452.
 *
 * Frame (link.w A6,-0x8; D2/D3/A5 saved):
 *   (0x8,A6)   virtual_addr    longword -> D2, D1 = D2 >> 15
 *   (0xc,A6)   segno_out       pointer  -> A0
 *   (0x10,A6)  default_result  word     -> D0w
 *
 * Re-emitted from the disassembly 2026-09-19: the memory-top test is
 * `cmp.l D3,D1` (0x00E0DCEE) on the full 32-bit segment number against the
 * zero-extended SEG_MEM_TOP; the previous C truncated the segment number to
 * a word first, so a VA of 0x10000000 or more folded back into private A.
 */

#include "mst/mst_internal.h"

/*
 * @param virtual_addr   The virtual address to translate
 * @param segno_out      Output: MST index
 * @param default_result Value to return for a private segment
 * @return default_result for private, 0 for global / out of range, 0x3a for
 *         the global A .. global B gap
 */
uint16_t MST_$VA_TO_SEGNO(uint32_t virtual_addr, uint16_t *segno_out,
                          uint16_t default_result)
{
    uint32_t segno;        /* D1: `lsr.l #0x8` + `lsr.l #0x7` */
    uint16_t index;        /* D2w / D0w */

    /* 0x00E0DCDE .. 0x00E0DCE8 */
    segno = virtual_addr >> 15;

    /* 0x00E0DCE6 .. 0x00E0DCF0: `clr.l D3` / `move.w (0x148,A5),D3w` /
     * `cmp.l D3,D1` / `bcc.b 0x00e0dd30` - unsigned 32-bit compare */
    if (segno >= (uint32_t)MST_$SEG_MEM_TOP) {
        return 0;                                   /* 0x00E0DD30 clr.w D0w */
    }

    /* 0x00E0DCF2 .. 0x00E0DCF8: `cmp.w (0x158,A5),D2w` / `bcs.b` */
    index = (uint16_t)segno;
    if (index < MST_$PRIVATE_A_SIZE) {
        *segno_out = index;                         /* 0x00E0DD0A */
        return default_result;
    }

    /* 0x00E0DCFA .. 0x00E0DD04: D2w = segno - SEG_PRIVATE_B; `cmpi.w #0x8` /
     * `bcc.b` - the eight private B segments */
    index = (uint16_t)((uint16_t)segno - MST_$SEG_PRIVATE_B);
    if (index < 8) {
        index = (uint16_t)(index + MST_$PRIVATE_A_SIZE);   /* 0x00E0DD06 */
        *segno_out = index;                         /* 0x00E0DD0A */
        return default_result;
    }

    /* 0x00E0DD0E .. 0x00E0DD1A: D0w = segno - SEG_GLOBAL_A; `cmp.w
     * (0x15e,A5),D0w` / `bcc.b` */
    index = (uint16_t)((uint16_t)segno - MST_$SEG_GLOBAL_A);
    if (index < MST_$GLOBAL_A_SIZE) {
        *segno_out = index;                         /* 0x00E0DD1C */
        return 0;                                   /* 0x00E0DD30 */
    }

    /* 0x00E0DD20 .. 0x00E0DD24: `cmp.w (0x14e,A5),D1w` / `bcs.b 0x00e0dd34` */
    if ((uint16_t)segno < MST_$SEG_GLOBAL_B) {
        return 0x3a;                                /* 0x00E0DD34 moveq #0x3a */
    }

    /* 0x00E0DD26 .. 0x00E0DD2E: D1w = segno - SEG_GLOBAL_B + GLOBAL_A_SIZE */
    *segno_out = (uint16_t)((uint16_t)segno - MST_$SEG_GLOBAL_B + MST_$GLOBAL_A_SIZE);
    return 0;                                       /* 0x00E0DD30 */
}
