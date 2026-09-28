/*
 * MST_$FREE_ASID - Free an Address Space ID and clean up its mappings
 *
 * Original address: 0x00E74B3C (SAU2 map: MST_UNWIRED segment E74A74,
 * size 0x1B0; MST_$FREE_ASID at E74B3C)
 * Size: 230 bytes (0x00E74B3C .. 0x00E74C21)
 *
 * The routine unmaps every private-A and private-B segment of the ASID
 * with MST_$UNMAP_PRIVI, crashing the system if either unmap fails; then
 * it hands the ASID's areas back with AREA_$FREE_ASID and, under ML lock
 * 0x0C, unwires the ASID's MST pages and clears its bit in the ASID set.
 *
 * Frame (link.w A6,0x0; D2/D3/A2/A5 saved; A5 = 0xE85714 at 0x00E74B44,
 * the module data base, which this body never dereferences):
 *   (0x8,A6)   asid         word  -> D2w
 *   (0xa,A6)   status_ret   pointer -> A2
 *
 * The UID cell pushed to both unmaps is `move.l #0xe1737c` = &UID_$NIL.
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "mst/mst_internal.h"
#include "misc/misc.h"

/*
 * @param asid        The ASID to free
 * @param status_ret  Output: status code
 */
void MST_$FREE_ASID(uint16_t asid, status_$t *status_ret)
{
    uint16_t start_page;   /* MST_ASID_BASE[asid] */
    uint16_t end_page;     /* D0w */

    /*
     * 0x00E74B52 .. 0x00E74B76: MST_$UNMAP_PRIVI(1, &UID_$NIL, 0,
     * MST_$PRIVATE_A_SIZE << 15, asid, status_ret).  The size is the
     * word at 0xE2445C zero-extended (`clr.l D0` / `move.w`) then
     * `lsl.l #0x8` + `lsl.l #0x7`.
     */
    MST_$UNMAP_PRIVI(1,
                     &UID_$NIL,
                     0,
                     (uint32_t)MST_$PRIVATE_A_SIZE << 15,
                     asid,
                     status_ret);

    /* 0x00E74B7A tst.l (A2) / bne.w 0x00E74C10 */
    if (*status_ret != status_$ok) {
        /* 0x00E74C10 .. 0x00E74C16: CRASH_SYSTEM(status_ret), then epilogue */
        CRASH_SYSTEM(status_ret);
        return;
    }

    /*
     * 0x00E74B80 .. 0x00E74BA8: MST_$UNMAP_PRIVI(1, &UID_$NIL,
     * MST_$SEG_PRIVATE_B << 15, 0x40000, asid, status_ret) - the eight
     * private-B segments (8 * 0x8000 = 0x40000 bytes).
     */
    MST_$UNMAP_PRIVI(1,
                     &UID_$NIL,
                     (uint32_t)MST_$SEG_PRIVATE_B << 15,
                     0x40000,
                     asid,
                     status_ret);

    /* 0x00E74BAC tst.l (A2) / bne.b 0x00E74C10 */
    if (*status_ret != status_$ok) {
        CRASH_SYSTEM(status_ret);
        return;
    }

    /* 0x00E74BB0 .. 0x00E74BBA: AREA_$FREE_ASID(asid), with a result slot */
    AREA_$FREE_ASID(asid);

    /* 0x00E74BBC .. 0x00E74BC8: ML_$LOCK(0xC) */
    ML_$LOCK(MST_LOCK_ASID);

    /*
     * 0x00E74BCA .. 0x00E74BEC: D0w = (MST_$SEG_TN >> 6) + MST_ASID_BASE[asid]
     * - 1 (`lsr.w #0x6` on the word at 0xE24464, `add.w (0x0,A0,D3w*0x1)`,
     * `subq.w #0x1`); mst_$unwire_asid_pages(MST_ASID_BASE[asid], D0w).
     */
    start_page = MST_ASID_BASE[asid];
    end_page = (uint16_t)((MST_$SEG_TN >> 6) + start_page - 1);
    mst_$unwire_asid_pages(start_page, end_page);

    /* 0x00E74BEE .. 0x00E74C00: MST_$SET_CLEAR(&MST_$ASID_LIST, 0x3a, asid) */
    MST_$SET_CLEAR(MST_$ASID_LIST, MST_MAX_ASIDS, asid);

    /* 0x00E74C02 .. 0x00E74C0E: ML_$UNLOCK(0xC) (no addq; unlk discards) */
    ML_$UNLOCK(MST_LOCK_ASID);
}
