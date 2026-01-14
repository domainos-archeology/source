/*
 * MMAP_$FREE_WSL - Free a process's working set list
 *
 * Releases the WSL associated with a process. If no other process
 * is using the same WSL, purges all pages and marks the WSL as free.
 *
 * Original address: 0x00e0d158
 */

#include "mmap.h"
#include "misc/misc.h"

void MMAP_$FREE_WSL(uint16_t pid)
{
    if (pid > MMAP_MAX_PID) {
        CRASH_SYSTEM(Illegal_PID_Err);
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
