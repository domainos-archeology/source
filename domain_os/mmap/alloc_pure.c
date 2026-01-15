/*
 * MMAP_$ALLOC_PURE - Allocate pure (code) pages
 *
 * Attempts to allocate pages from the pure page pools (types 0 and 1).
 * If insufficient pages are available, may trigger page stealing from
 * the current process's working set.
 *
 * Original address: 0x00e0d78e
 */

#include "mmap_internal.h"
#include "proc1/proc1.h"

uint16_t MMAP_$ALLOC_PURE(uint32_t *vpn_array, uint16_t count)
{
    MMAP_$ALLOC_CNT++;

    uint16_t allocated = 0;
    boolean tried_steal = false;

    while (count > 0) {
        /* Try to allocate from pure page pools (indices 0 and 1) */
        for (uint16_t pool = 0; pool <= 1; pool++) {
            ws_hdr_t *wsl = WSL_FOR_INDEX(pool);
            if (wsl->page_count > 0) {
                uint16_t to_alloc = (wsl->page_count < count) ?
                                    (uint16_t)wsl->page_count : count;

                mmap_$alloc_pages_from_wsl(wsl, vpn_array + allocated, to_alloc);
                count -= to_alloc;
                allocated += to_alloc;

                if (count == 0) goto done;
            }
        }

        /* Not enough pages - try stealing if conditions allow */
        if (count == 0) break;
        if (allocated >= 8) break;  /* Got enough */

        MMAP_$STEAL_CNT++;

        if (tried_steal) break;

        /* Check if stealing is worthwhile */
        if (DAT_00e23344 + DAT_00e23320 > 8) break;

        uint16_t wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];
        ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);

        if (wsl->page_count < 0x180) break;  /* Not enough to steal from */

        /* Trim pages from current process's WSL */
        mmap_$trim_wsl(wsl_index, count);
        tried_steal = true;
    }

done:
    MMAP_$ALLOC_PAGES += allocated;
    return allocated;
}
