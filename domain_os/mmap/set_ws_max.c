/*
 * MMAP_$SET_WS_MAX - Set maximum pages for a working set list
 *
 * Sets the maximum number of pages allowed in a working set.
 * The WSL index must be in the valid user range (5-69).
 *
 * Original address: 0x00e0ca7a
 */

#include "mmap.h"

void MMAP_$SET_WS_MAX(uint16_t wsl_index, uint32_t max_pages, status_$t *status)
{
    *status = status_$ok;

    /* Validate WSL index is in the valid user range */
    if (wsl_index < WSL_INDEX_MIN_USER || wsl_index > WSL_INDEX_MAX) {
        *status = status_$mmap_illegal_wsl_index;
        return;
    }

    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
    wsl->max_pages = max_pages;
}
