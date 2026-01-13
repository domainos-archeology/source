/*
 * MMAP_$GET_WS_SIZ - Get working set size information
 *
 * Returns size information for a working set list: page count,
 * maximum allowed pages, and an additional field.
 *
 * Original address: 0x00e0c9e4
 */

#include "mmap.h"

void MMAP_$GET_WS_SIZ(uint16_t wsl_index, uint32_t *page_count,
                       uint32_t *field_40, uint32_t *max_pages,
                       status_$t *status)
{
    *status = status_$ok;

    if (wsl_index > WSL_INDEX_MAX) {
        *status = status_$mmap_illegal_wsl_index;
        return;
    }

    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
    *page_count = wsl->page_count;
    *field_40 = wsl->field_14;
    *max_pages = wsl->max_pages;
}
