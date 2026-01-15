/*
 * MMAP_$AVAIL - Make a page available in its designated WSL
 *
 * Adds a page to the working set list specified in the page's
 * wsl_index field. Validates the WSL index first.
 *
 * Original address: 0x00e0cc64
 */

#include "mmap.h"
#include "mmap_internal.h"
#include "misc/misc.h"

void MMAP_$AVAIL(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);

    /* Validate WSL index */
    if (page->wsl_index > WSL_INDEX_MAX) {
        CRASH_SYSTEM(mmap_bad_avail);
    }

    /* Add to the WSL specified in the page entry, inserting at tail */
    mmap_$add_to_wsl(page, vpn, page->wsl_index, -1);
}
