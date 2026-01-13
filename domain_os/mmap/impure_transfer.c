/*
 * MMAP_$IMPURE_TRANSFER - Transfer impure page to dirty list
 *
 * For pages that are marked as impure (types 3 or 4), removes them
 * from their current WSL and adds them to the appropriate dirty list.
 *
 * Original address: 0x00e0cbe8
 */

#include "mmap.h"

void MMAP_$IMPURE_TRANSFER(mmape_t *page, uint32_t vpn)
{
    uint8_t page_type = page->wsl_index;

    /* Only process impure page types (3 = dirty no-flush, 4 = dirty flush) */
    if (page_type == MMAP_PAGE_TYPE_DIRTY_FL || page_type == MMAP_PAGE_TYPE_DIRTY_NF) {
        /* Remove from current WSL */
        mmap_$remove_from_wsl(page, vpn);

        /* Add to impure list (type 1), don't update head */
        mmap_$add_to_wsl(page, vpn, MMAP_PAGE_TYPE_PURE, 0);
    }
}
