/*
 * MMAP_$WIRE - Wire a page (prevent paging)
 *
 * Increments the wire count on a page to prevent it from being
 * paged out. If the page was previously unwired and is in a user
 * WSL (index < 5), removes it from the user WSL to the wired pool.
 *
 * Original address: 0x00e0ccbc
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
/* 0x00E0CCE4: pea (0x32,PC) -> 0x00E0CD18, jsr CRASH_SYSTEM at 0x00E0CCE8. */
static const status_$t mmap_$bad_unavail_00e0cd18 = 0x00060006;

#define MAX_WIRE_COUNT  0x39  /* ASCII '9' */

void MMAP_$WIRE(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);

    /* Check for wire count overflow */
    if (page->wire_count == MAX_WIRE_COUNT) {
        CRASH_SYSTEM(&mmap_$bad_unavail_00e0cd18);
    }

    page->wire_count++;

    /* If page was just wired (flags2 bit 7 set) and in user WSL */
    if ((page->flags2 & MMAPE_FLAG2_ON_DISK) && page->wsl_index < WSL_INDEX_MIN_USER) {
        /* Remove from user WSL to wired pool */
        mmap_$remove_from_wsl(page, vpn);
    }
}
