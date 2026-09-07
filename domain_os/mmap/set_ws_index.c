/*
 * MMAP_$SET_WS_INDEX - Set/allocate WSL index for a process
 *
 * Associates a process with a working set list. If the input
 * wsl_index is 0, allocates a new WSL. Otherwise validates and
 * uses the provided index.
 *
 * Original address: 0x00e0d1c8
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * 0x00E0D1E4: pea (-0x22,PC) -> 0x00E0D1C4, jsr CRASH_SYSTEM at 0x00E0D1E8.
 * The same cell is used by MMAP_$FREE_WSL (0x00E0D170).
 */
static const status_$t mmap_$illegal_pid_00e0d1c4 = 0x0006000A;
/*
 * 0x00E0D22E: pea (-0x850,PC) -> 0x00E0C9E0, jsr CRASH_SYSTEM at 0x00E0D232.
 * Shared with MMAP_$SET_WS_PRI, MMAP_$PURGE and MMAP_$WS_SCAN.
 */
static const status_$t mmap_$illegal_wsl_index_00e0c9e0 = 0x00060009;
/* 0x00E0D23E: pea (0x30,PC) -> 0x00E0D270, jsr CRASH_SYSTEM at 0x00E0D242. */
static const status_$t mmap_$ws_lists_exhausted_00e0d270 = 0x0006000B;

void MMAP_$SET_WS_INDEX(uint16_t pid, uint16_t *wsl_index)
{
    if (pid > MMAP_MAX_PID) {
        CRASH_SYSTEM(&mmap_$illegal_pid_00e0d1c4);
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
            CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0);
        }
        goto found;
    }

    if (*wsl_index == 0) {
        CRASH_SYSTEM(&mmap_$ws_lists_exhausted_00e0d270);
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
