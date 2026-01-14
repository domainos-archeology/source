/*
 * PROC2_$FIND_ASID - Find address space ID for process
 *
 * Looks up a process by UID and returns either its UPID
 * or another ID depending on flags.
 *
 * Parameters:
 *   proc_uid - UID of process to look up
 *   param_2 - Flag byte (if negative and flag 0x800 set, use alt field)
 *   status_ret - Status return
 *
 * Returns:
 *   Process ID value
 *
 * Original address: 0x00e40724
 */

#include "proc2.h"

uint16_t PROC2_$FIND_ASID(uid_t *proc_uid, int8_t *param_2, status_$t *status_ret)
{
    int16_t index;
    proc2_info_t *entry;
    status_$t status;
    uint16_t result = 0;

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(proc_uid, &status);

    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(index);

        /* Check if param_2 is negative and flag 0x800 is set */
        if ((*param_2 < 0) && ((entry->flags & 0x0800) != 0)) {
            /* Use alternate ASID at offset 0x98 */
            result = entry->asid_alt;
        } else {
            /* Use primary ASID at offset 0x96 */
            result = entry->asid;
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
    return result;
}
