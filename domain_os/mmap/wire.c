/*
 * MMAP_$WIRE - Wire a page (prevent paging)
 *
 * Increments the wire count on a page to prevent it from being
 * paged out. If the page was previously unwired and is in a user
 * WSL (index < 5), removes it from the user WSL to the wired pool.
 *
 * Original address: 0x00e0ccbc
 */

#include "mmap.h"

#define MAX_WIRE_COUNT  0x39  /* ASCII '9' */

void MMAP_$WIRE(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);

    /* Check for wire count overflow */
    if (page->wire_count == MAX_WIRE_COUNT) {
        CRASH_SYSTEM(MMAP_Bad_Unavail_err);
    }

    page->wire_count++;

    /* If page was just wired (flags2 bit 7 set) and in user WSL */
    if ((page->flags2 & MMAPE_FLAG2_ON_DISK) && page->wsl_index < WSL_INDEX_MIN_USER) {
        /* Remove from user WSL to wired pool */
        mmap_$remove_from_wsl(page, vpn);
    }
}
