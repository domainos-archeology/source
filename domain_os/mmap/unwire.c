/*
 * MMAP_$UNWIRE - Drop one wire on a page; a fully unwired resident page
 * rejoins the current process's working set
 *
 * Original address: 0x00E0CD1C (100 bytes; `E0CD1C MMAP_$UNWIRE` in the
 * SAU2 map).  No caller in the image (`gsk xrefs to 00e0cd1c` is empty).
 *
 * Frame (0x00E0CD1C-0x00E0CD2A): `link.w A6,#-4`, D2/A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  One argument: (0x8,A6) vpn, longword (D2).
 *
 * 0x00E0CD2E-0x00E0CD38  A2 = 0xEB4800 + vpn*0x10
 * 0x00E0CD3C-0x00E0CD4C  wire_count == 0 -> CRASH_SYSTEM(&mmap_$bad_avail
 *                        _00e0ccb8), the cell shared with MMAP_$AVAIL
 * 0x00E0CD4E-0x00E0CD52  `subq.b #1' wire_count; still non-zero -> done
 * 0x00E0CD54-0x00E0CD58  flags1 bit 7 (IN_WSL) set -> done (`tst.b / bmi'
 *                        on -0x1FFB = flags1, not flags2)
 * 0x00E0CD5A-0x00E0CD72  mmap_$add_to_wsl(page, vpn,
 *                        MMAP_PID_TO_WSL[PROC1_$CURRENT], true)
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
#include "proc1/proc1.h"

void MMAP_$UNWIRE(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);                      /* 0x00E0CD2E */

    if (page->wire_count == 0) {                             /* 0x00E0CD3C */
        CRASH_SYSTEM(&mmap_$bad_avail_00e0ccb8);             /* 0x00E0CD46 */
    }

    page->wire_count--;                                      /* 0x00E0CD4E */
    if (page->wire_count != 0) {                             /* 0x00E0CD52 */
        return;
    }
    if (page->flags1 & MMAPE_FLAG1_IN_WSL) {                 /* 0x00E0CD54 */
        return;
    }

    mmap_$add_to_wsl(page, vpn, MMAP_PID_TO_WSL[PROC1_$CURRENT], true); /* 0x00E0CD72 */
}
