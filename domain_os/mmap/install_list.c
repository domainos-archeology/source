/*
 * MMAP_$INSTALL_LIST - Install a list of pages into a WSL
 *
 * Adds an array of pages to either the wired WSL (index 5) or
 * the current process's WSL. Checks for working set overflow.
 *
 * Original address: 0x00e0cd80
 */

#include "mmap.h"

void MMAP_$INSTALL_LIST(uint32_t *vpn_array, uint16_t count, int8_t use_wired)
{
    uint16_t wsl_index;

    if (use_wired < 0) {
        wsl_index = WSL_INDEX_WIRED;
    } else {
        wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];
    }

    mmap_$add_pages_to_wsl(vpn_array, count, wsl_index);

    /* Check for working set overflow */
    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
    if (wsl->max_pages < wsl->page_count) {
        mmap_$trim_wsl(wsl_index, wsl->page_count - wsl->max_pages);
        MMAP_$WS_OVERFLOW++;
    }
}
