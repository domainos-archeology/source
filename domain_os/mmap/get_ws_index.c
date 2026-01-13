/*
 * MMAP_$GET_WS_INDEX - Get WSL index for a process ID
 *
 * Returns the working set list index associated with a given process ID.
 *
 * Original address: 0x00e0ca3a
 */

#include "mmap.h"

void MMAP_$GET_WS_INDEX(uint16_t pid, uint16_t *wsl_index, status_$t *status)
{
    *status = status_$ok;

    if (pid > MMAP_MAX_PID) {
        *status = status_$mmap_illegal_pid;
        return;
    }

    *wsl_index = MMAP_PID_TO_WSL[pid];
}
