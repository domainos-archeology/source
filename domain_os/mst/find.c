/*
 * MST_$FIND - Find the physical page for a virtual address
 *
 * Original address: 0x00E0E11E (SAU2 map: MST_WIRED, MST_$FIND at E0E11E)
 * Size: 154 bytes (0x00E0E11E .. 0x00E0E1B7); the status cell it crashes
 * with follows the body at 0x00E0E1B8.
 *
 * Under ML lock 0x14 the routine asks MMU_$VTOP for the translation.  If
 * the page is present it is optionally wired (flag bit 1) and its physical
 * page is returned; otherwise the lock is dropped and MST_$TOUCH is asked
 * to fault the page in, with the same bit deciding whether it is wired.
 *
 * Frame (link.w A6,-0xc; D2/D3 saved):
 *   (0x8,A6)   virt_addr  longword
 *   (0xc,A6)   flags      word (`move.w (0xc,A6),D2w` at 0x00E0E126)
 *   (-0x8,A6)  status     the status_$t MMU_$VTOP / MST_$TOUCH fill in
 *   D3         ppn        MMU_$VTOP's result
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "mst/mst_internal.h"
#include "misc/misc.h"

/*
 * Status cell passed to CRASH_SYSTEM by `pea (0x86,PC)` at 0x00E0E130
 * (-> 0x00E0E1B8, image bytes 00 04 00 05): MST "reference to out-of-bounds
 * address".  It is a constant longword in this module's own code region.
 */
static const status_$t mst_$ref_out_of_bounds_00e0e1b8 = 0x00040005;

/*
 * @param virt_addr  Virtual address to look up
 * @param flags      bit 1 = wire the page; bits 0 and 2 must be clear
 * @return The physical page (D0): MMU_$VTOP's result when the page is
 *         present, otherwise whatever MST_$TOUCH returns
 */
uint32_t MST_$FIND(uint32_t virt_addr, uint16_t flags)
{
    uint32_t ppn;          /* D3 */
    status_$t status;      /* (-0x8,A6) */

    /*
     * 0x00E0E12A .. 0x00E0E13A: `moveq #0x5,D0` / `and.w D2w,D0w` - bits 0
     * or 2 of the flags word are a fatal caller error.  CRASH_SYSTEM is
     * called and the code simply continues afterwards.
     */
    if ((flags & 5) != 0) {
        CRASH_SYSTEM(&mst_$ref_out_of_bounds_00e0e1b8);
    }

    /* 0x00E0E13C .. 0x00E0E148: ML_$LOCK(0x14) */
    ML_$LOCK(MST_LOCK_MMU);

    /* 0x00E0E14A .. 0x00E0E15A: ppn = MMU_$VTOP(virt_addr, &status) */
    ppn = MMU_$VTOP(virt_addr, &status);

    /* 0x00E0E15C tst.l (-0x8,A6) */
    if (status == status_$ok) {
        /* 0x00E0E162 .. 0x00E0E170: `btst.l #0x1,D2` -> MMAP_$WIRE(ppn) */
        if ((flags & 2) != 0) {
            MMAP_$WIRE(ppn);
        }
        /* 0x00E0E172 .. 0x00E0E180: ML_$UNLOCK(0x14); D0 = ppn */
        ML_$UNLOCK(MST_LOCK_MMU);
        return ppn;
    }

    /* 0x00E0E182 .. 0x00E0E18E: ML_$UNLOCK(0x14) */
    ML_$UNLOCK(MST_LOCK_MMU);

    /*
     * 0x00E0E190 .. 0x00E0E1AA: MST_$TOUCH(virt_addr, &status, wire) with a
     * Pascal result slot; wire is the word 1 when flag bit 1 is set
     * (`move.w #0x1,-(SP)`), else 0 (`clr.w -(SP)`).  Its D0 is returned.
     */
    return MST_$TOUCH(virt_addr, &status, ((flags & 2) != 0) ? 1 : 0);
}
