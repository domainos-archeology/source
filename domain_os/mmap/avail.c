/*
 * MMAP_$AVAIL - Make a page available in its designated WSL
 *
 * Adds a page to the working set list specified in the page's
 * wsl_index field. Validates the WSL index first.
 *
 * Original address: 0x00e0cc64
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * 0x00E0CC8C: pea (0x2a,PC) -> 0x00E0CCB8, jsr CRASH_SYSTEM at 0x00E0CC90.
 * The same cell is used by MMAP_$UNWIRE (0x00E0CD42).
 */
static const status_$t mmap_$bad_avail_00e0ccb8 = 0x00060004;

void MMAP_$AVAIL(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);

    /* Validate WSL index */
    if (page->wsl_index > WSL_INDEX_MAX) {
        CRASH_SYSTEM(&mmap_$bad_avail_00e0ccb8);
    }

    /* Add to the WSL specified in the page entry, inserting at tail */
    mmap_$add_to_wsl(page, vpn, page->wsl_index, -1);
}
