/*
 * FILE_$UNLOCK_ALL - Unlock all locks for current process
 *
 * Original address: 0x00E75138
 * Size: 32 bytes
 *
 * Releases all file locks held by the current process. This is
 * typically called during process termination or cleanup.
 *
 * Assembly analysis:
 *   - link.w A6,0x0           ; No stack frame
 *   - Saves A5 to stack
 *   - Loads A5 with 0xE86068 (global base)
 *   - Pushes #0xE2060A (address of PROC1_$AS_ID)
 *   - Calls FILE_$PRIV_UNLOCK_ALL
 *   - Restores A5 and returns
 */

#include "file/file_internal.h"

/*
 * FILE_$UNLOCK_ALL - Unlock all locks for current process
 *
 * Releases all file locks held by the current process (identified
 * by PROC1_$AS_ID). This function is typically called during
 * process cleanup to ensure no stale locks remain.
 *
 * The function calls FILE_$PRIV_UNLOCK_ALL with a pointer to the
 * current process's ASID, which iterates through all lock entries
 * and releases those owned by this process.
 *
 * Takes no parameters and returns no status.
 */
void FILE_$UNLOCK_ALL(void)
{
    /*
     * Call internal unlock function with current process ASID
     * PROC1_$AS_ID is declared in proc1/proc1.h
     */
    FILE_$PRIV_UNLOCK_ALL(&PROC1_$AS_ID);
}
