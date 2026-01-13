/*
 * MMAP_$SET_WS_INDEX - Set/allocate WSL index for a process
 *
 * Associates a process with a working set list. If the input
 * wsl_index is 0, allocates a new WSL. Otherwise validates and
 * uses the provided index.
 *
 * Original address: 0x00e0d1c8
 */

#include "mmap.h"

void MMAP_$SET_WS_INDEX(uint16_t pid, uint16_t *wsl_index)
{
    if (pid > MMAP_MAX_PID) {
        CRASH_SYSTEM(Illegal_PID_Err);
    }

    if (*wsl_index == 0) {
        /* Allocate a new WSL - scan for free slot starting at index 8 */
        for (uint16_t i = 8; i <= WSL_INDEX_MAX + 1; i++) {
            ws_hdr_t *wsl = WSL_FOR_INDEX(i);
            if (!(wsl->flags & WSL_FLAG_IN_USE)) {
                *wsl_index = i;

                /* Update high water mark if needed */
                if (i > MMAP_WSL_HI_MARK) {
                    MMAP_WSL_HI_MARK = i;
                }
                goto found;
            }
        }
        /* No free WSL available */
    } else {
        /* Validate provided WSL index */
        if (*wsl_index < WSL_INDEX_MIN_USER || *wsl_index > MMAP_WSL_HI_MARK) {
            CRASH_SYSTEM(Illegal_WSL_Index_Err);
        }
        goto found;
    }

    if (*wsl_index == 0) {
        CRASH_SYSTEM(WSL_Exhausted_Err);
        return;
    }

found:
    /* Mark WSL as in use and associate with process */
    {
        ws_hdr_t *wsl = WSL_FOR_INDEX(*wsl_index);
        wsl->flags |= WSL_FLAG_IN_USE;
        MMAP_PID_TO_WSL[pid] = *wsl_index;
    }
}
