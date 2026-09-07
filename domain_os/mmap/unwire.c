/*
 * MMAP_$UNWIRE - Unwire a page (allow paging)
 *
 * Decrements the wire count on a page. When the wire count reaches
 * zero and the page is not marked as on-disk, adds it back to the
 * current process's working set list.
 *
 * Original address: 0x00e0cd1c
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
#include "proc1/proc1.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * 0x00E0CD42: pea (-0x8c,PC) -> 0x00E0CCB8, jsr CRASH_SYSTEM at 0x00E0CD46.
 * The same cell is used by MMAP_$AVAIL (0x00E0CC8C).
 */
static const status_$t mmap_$bad_avail_00e0ccb8 = 0x00060004;

void MMAP_$UNWIRE(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);

    /* Check for underflow */
    if (page->wire_count == 0) {
        CRASH_SYSTEM(&mmap_$bad_avail_00e0ccb8);
    }

    page->wire_count--;

    /* If fully unwired and not on disk, add to current process's WSL */
    if (page->wire_count == 0 && !(page->flags2 & MMAPE_FLAG2_ON_DISK)) {
        uint16_t wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];
        mmap_$add_to_wsl(page, vpn, wsl_index, -1);
    }
}
