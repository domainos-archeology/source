/*
 * MST_$PRE_INIT - Early initialization of MST segment configuration
 *
 * Original address: 0x00E309F4 (SAU2 map: MST_UNWIRED, MST_$PRE_INIT at
 * E309F4)
 * Size: 186 bytes (0x00E309F4 .. 0x00E30AAD)
 *
 * Called once from OS_$INIT (0x00E33838).  On a 68020 machine the fourteen
 * segment-layout words at 0xE2444A .. 0xE24464 are overwritten with the
 * large-address-space layout; then MST_ASID_BASE[0..57] is filled with
 * asid * ((MST_$SEG_TN + 63) div 64), the number of MST words each ASID
 * owns.
 *
 * Frame (link.w A6,-0x8; D2/D3 saved):
 *   D1w  dbf counter (0x39 -> 58 iterations)
 *   D2w  asid
 *   D3   words per ASID
 *
 * Verified against the disassembly 2026-09-19; the body was already
 * faithful, the signed-division fix-up at 0x00E30A86 is now reproduced.
 */

#include "mst/mst_internal.h"

void MST_$PRE_INIT(void)
{
    int16_t counter;              /* D1w */
    uint16_t asid;                /* D2w */
    int32_t words_per_asid;       /* D3 */

    /*
     * 0x00E309FC `tst.b (0x00e23d2e).l` / `bpl.b 0x00e30a74`: the high
     * byte of M68020 negative means a 68020, which gets the big layout.
     */
    if (MST_M68020_IS_020()) {
        MST_$SEG_TN = 0x680;               /* 0x00E30A04 -> 0xE24464 */
        MST_$GLOBAL_A_SIZE = 0xe0;         /* 0x00E30A0C -> 0xE24462 */
        MST_$SEG_GLOBAL_A = 0x678;         /* 0x00E30A14 -> 0xE24460 */
        MST_$SEG_GLOBAL_A_END = 0x757;     /* 0x00E30A1C -> 0xE2445E */
        MST_$PRIVATE_A_SIZE = 0x678;       /* 0x00E30A24 -> 0xE2445C */
        MST_$SEG_PRIVATE_A_END = 0x677;    /* 0x00E30A2C -> 0xE2445A */
        MST_$SEG_PRIVATE_B = 0x758;        /* 0x00E30A34 -> 0xE24458 */
        MST_$SEG_PRIVATE_B_END = 0x75f;    /* 0x00E30A3C -> 0xE24456 */
        MST_$SEG_PRIVATE_B_OFFSET = 0xe0;  /* 0x00E30A44 -> 0xE24454 */
        MST_$SEG_GLOBAL_B = 0x760;         /* 0x00E30A4C -> 0xE24452 */
        MST_$SEG_GLOBAL_B_OFFSET = 0x680;  /* 0x00E30A54 -> 0xE24450 */
        MST_$SEG_HIGH = 0x7e0;             /* 0x00E30A5C -> 0xE2444E */
        MST_$SEG_MEM_TOP = 0x800;          /* 0x00E30A64 -> 0xE2444C */
        MST_$GLOBAL_B_SIZE = 0xa0;         /* 0x00E30A6C -> 0xE2444A */
    }

    /*
     * 0x00E30A76 .. 0x00E30A8E: D3 = MST_$SEG_TN zero-extended
     * (`clr.l D3` / `move.w`), plus 0x3f; the compiler's signed `div 64`
     * adds 0x3f again when the sum is negative (`bpl.b` / `addi.l #0x3f`,
     * never taken for a zero-extended word) and then `asr.l #0x6`.
     */
    words_per_asid = (int32_t)(uint32_t)MST_$SEG_TN + 0x3f;
    if (words_per_asid < 0) {
        words_per_asid += 0x3f;
    }
    words_per_asid >>= 6;

    /*
     * 0x00E30A74 `moveq #0x39,D1` .. 0x00E30AA0 `dbf`: 58 entries.
     * `muls.w D3w,D0` multiplies the two low words as signed 16-bit values;
     * only the low word of the product is stored (`move.w D0w,(A0)+`).
     */
    asid = 0;
    for (counter = 0x39; counter != -1; counter--) {
        MST_ASID_BASE[asid] = (uint16_t)((int32_t)(int16_t)asid * (int16_t)words_per_asid);
        asid++;
    }
}
