/*
 * MMAP_$SET_WS_PRI - Set working set priority timestamp
 *
 * Updates the priority timestamp for the current process's working set list
 * to the current high-resolution clock value. This is used for page replacement
 * priority calculations.
 *
 * Original address: 0x00e0c98a
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
#include "proc1/proc1.h"
#include "time/time.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * 0x00E0C9B8: pea (0x26,PC) -> 0x00E0C9E0, jsr CRASH_SYSTEM at 0x00E0C9BC.
 * Shared with MMAP_$PURGE, MMAP_$WS_SCAN and MMAP_$SET_WS_INDEX.
 */
static const status_$t mmap_$illegal_wsl_index_00e0c9e0 = 0x00060009;

void MMAP_$SET_WS_PRI(void)
{
    uint16_t wsl_index;

    /* Get the WSL index for the current process */
    wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];

    /* Validate WSL index is in the valid user range (5-69) */
    if (wsl_index <= WSL_INDEX_MAX && wsl_index > 4) {
        /* Update the priority timestamp for this working set */
        ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
        wsl->pri_timestamp = TIME_$CLOCKH;
    } else if (wsl_index != 0) {
        /* Invalid WSL index - crash the system */
        CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0);
    }
    /* wsl_index == 0 means no WSL assigned, silently ignore */
}
