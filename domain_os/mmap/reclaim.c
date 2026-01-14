/*
 * MMAP_$RECLAIM - Reclaim pages into a working set list
 *
 * Reclaims pages that were previously removed from a WSL (e.g.,
 * during page-out) back into the working set. Only pages that
 * are in the low-numbered pools (0-4) and are marked as in-WSL
 * are reclaimed.
 *
 * Original address: 0x00e0d914
 */

#include "mmap.h"
#include "misc/misc.h"

void MMAP_$RECLAIM(uint32_t *vpn_array, uint16_t count, int8_t use_wired)
{
    uint16_t wsl_index;

    if (use_wired < 0) {
        wsl_index = WSL_INDEX_WIRED;
    } else {
        wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];
    }

    boolean reclaimed_any = false;

    for (uint16_t i = 0; i < count; i++) {
        uint32_t vpn = vpn_array[i];
        mmape_t *page = MMAPE_FOR_VPN(vpn);

        /* Only reclaim pages that are in pools 0-4 and marked as in-WSL */
        if (page->wsl_index >= WSL_INDEX_MIN_USER) continue;
        if (!(page->flags1 & MMAPE_FLAG1_IN_WSL)) continue;

        /* Validate page is not in the free pool */
        if (page->wsl_index == WSL_INDEX_FREE_POOL) {
            CRASH_SYSTEM(MMAP_Bad_Reclaim_Err);
        }

        /* Remove from current pool */
        mmap_$remove_from_wsl(page, vpn);

        /* Add to target WSL */
        mmap_$add_to_wsl(page, vpn, wsl_index, -1);

        reclaimed_any = true;
    }

    /* Check for working set overflow */
    if (reclaimed_any) {
        ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
        if (wsl->max_pages < wsl->page_count) {
            mmap_$trim_wsl(wsl_index, wsl->page_count - wsl->max_pages);
            MMAP_$WS_OVERFLOW++;
        }
    }
}
