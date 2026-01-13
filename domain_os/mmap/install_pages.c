/*
 * MMAP_$INSTALL_PAGES - Install pages for a specific process
 *
 * Adds an array of pages to the WSL associated with a specific
 * process ID. Checks for working set overflow.
 *
 * Original address: 0x00e0cdf0
 */

#include "mmap.h"

void MMAP_$INSTALL_PAGES(uint32_t *vpn_array, uint16_t count, uint16_t pid)
{
    uint16_t wsl_index = MMAP_PID_TO_WSL[pid];

    mmap_$add_pages_to_wsl(vpn_array, count, wsl_index);

    /* Check for working set overflow */
    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
    if (wsl->max_pages < wsl->page_count) {
        mmap_$trim_wsl(wsl_index, wsl->page_count - wsl->max_pages);
        MMAP_$WS_OVERFLOW++;
    }
}
