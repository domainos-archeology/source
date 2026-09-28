/*
 * MST_$WIRE - Wire a virtual page into physical memory
 *
 * Original address: 0x00E0E1BC (SAU2 map: MST_WIRED, MST_$WIRE at E0E1BC,
 * the last routine of the segment)
 * Size: 122 bytes (0x00E0E1BC .. 0x00E0E235)
 *
 * Under ML lock 0x14 the routine asks MMU_$VTOP for the page.  If present
 * it is wired with MMAP_$WIRE and returned; otherwise the lock is dropped
 * and MST_$TOUCH is asked to fault it in wired (wire flag 1).  Either way
 * the status the translation / touch left in the frame is copied out.
 *
 * Frame (link.w A6,-0xc; D2/A2 saved):
 *   (0x8,A6)   vpn         longword, the virtual address
 *   (0xc,A6)   status_ret  pointer -> A2
 *   (-0x8,A6)  status      the status_$t MMU_$VTOP / MST_$TOUCH fill in
 *   D2         ppn         MMU_$VTOP's result
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "mst/mst_internal.h"

/*
 * @param vpn         Virtual address of the page
 * @param status_ret  Output: status code
 * @return The physical page (D0): MMU_$VTOP's result when present,
 *         otherwise whatever MST_$TOUCH returns
 */
uint32_t MST_$WIRE(uint32_t vpn, status_$t *status_ret)
{
    uint32_t ppn;          /* D2 */
    status_$t status;      /* (-0x8,A6) */

    /* 0x00E0E1C4 .. 0x00E0E1D4: ML_$LOCK(0x14) */
    ML_$LOCK(MST_LOCK_MMU);

    /* 0x00E0E1D6 .. 0x00E0E1E6: ppn = MMU_$VTOP(vpn, &status) */
    ppn = MMU_$VTOP(vpn, &status);

    /* 0x00E0E1E8 tst.l (-0x8,A6) / bne.b 0x00e0e208 */
    if (status == status_$ok) {
        /* 0x00E0E1EE .. 0x00E0E1F6: MMAP_$WIRE(ppn) under the lock */
        MMAP_$WIRE(ppn);
        /* 0x00E0E1F8 .. 0x00E0E204: ML_$UNLOCK(0x14); D0 = ppn */
        ML_$UNLOCK(MST_LOCK_MMU);
    } else {
        /* 0x00E0E208 .. 0x00E0E214: ML_$UNLOCK(0x14) */
        ML_$UNLOCK(MST_LOCK_MMU);
        /* 0x00E0E216 .. 0x00E0E224: MST_$TOUCH(vpn, &status, 1) with a
         * Pascal result slot; its D0 is the result */
        ppn = MST_$TOUCH(vpn, &status, 1);
    }

    /* 0x00E0E228 move.l (-0x8,A6),(A2) */
    *status_ret = status;
    return ppn;
}
