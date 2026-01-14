/*
 * PROC2_$SHUTDOWN - Shutdown all other processes
 *
 * Iterates through all processes and suspends those that are
 * valid and not the current address space. Used during system
 * shutdown to stop all user processes.
 *
 * Note: This function takes no parameters and returns no status.
 * It silently ignores any errors from PROC2_$SUSPEND.
 *
 * Original address: 0x00e415c2
 */

#include "proc2.h"

void PROC2_$SHUTDOWN(void)
{
    int16_t index;
    proc2_info_t *info;
    status_$t status;

    /* Start at first allocated entry */
    index = P2_INFO_ALLOC_PTR;

    while (index != 0) {
        info = P2_INFO_ENTRY(index);

        /* Skip if this is our own address space */
        /* Skip if process is not valid (bit 8 of flags) */
        if (info->asid != PROC1_$AS_ID &&
            (info->flags & 0x0100) != 0) {
            /* Suspend this process */
            PROC2_$SUSPEND(&info->uid, &status);
        }

        /* Move to next process in allocation list */
        index = info->next_index;
    }
}
