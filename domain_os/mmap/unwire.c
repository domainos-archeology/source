/*
 * MMAP_$UNWIRE - Unwire a page (allow paging)
 *
 * Decrements the wire count on a page. When the wire count reaches
 * zero and the page is not marked as on-disk, adds it back to the
 * current process's working set list.
 *
 * Original address: 0x00e0cd1c
 */

#include "mmap.h"
#include "mmap_internal.h"
#include "misc/misc.h"
#include "proc1/proc1.h"

void MMAP_$UNWIRE(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);

    /* Check for underflow */
    if (page->wire_count == 0) {
        CRASH_SYSTEM(mmap_bad_avail);
    }

    page->wire_count--;

    /* If fully unwired and not on disk, add to current process's WSL */
    if (page->wire_count == 0 && !(page->flags2 & MMAPE_FLAG2_ON_DISK)) {
        uint16_t wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];
        mmap_$add_to_wsl(page, vpn, wsl_index, -1);
    }
}
