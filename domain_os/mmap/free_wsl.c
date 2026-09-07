/*
 * MMAP_$FREE_WSL - Free a process's working set list
 *
 * Releases the WSL associated with a process. If no other process
 * is using the same WSL, purges all pages and marks the WSL as free.
 *
 * Original address: 0x00e0d158
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
 * 0x00E0D170: pea (0x52,PC) -> 0x00E0D1C4, jsr CRASH_SYSTEM at 0x00E0D174.
 * The same cell is used by MMAP_$SET_WS_INDEX (0x00E0D1E4).
 */
static const status_$t mmap_$illegal_pid_00e0d1c4 = 0x0006000A;

void MMAP_$FREE_WSL(uint16_t pid)
{
    if (pid > MMAP_MAX_PID) {
        CRASH_SYSTEM(&mmap_$illegal_pid_00e0d1c4);
    }

    uint16_t wsl_index = MMAP_PID_TO_WSL[pid];

    /* Clear the pid-to-wsl mapping */
    MMAP_PID_TO_WSL[pid] = 0;

    /* Check if any other process is using this WSL */
    for (uint16_t i = 0; i <= MMAP_MAX_PID; i++) {
        if (MMAP_PID_TO_WSL[i] == wsl_index) {
            /* Another process is using this WSL, don't free it */
            return;
        }
    }

    /* No other users - purge and free the WSL */
    MMAP_$PURGE(wsl_index);

    /* Mark WSL as not in use */
    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
    wsl->flags &= ~WSL_FLAG_IN_USE;
}
