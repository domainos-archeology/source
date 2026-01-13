/*
 * PROC2_$SET_NAME - Set process name
 *
 * Sets the name associated with a process. Names are up to
 * 32 characters long.
 *
 * Parameters:
 *   name - Name string
 *   name_len - Pointer to name length (0-32)
 *   proc_uid - UID of process
 *   status_ret - Status return
 *
 * Original address: 0x00e3ea2c
 */

#include "proc2.h"

/* Status code for invalid name */
#define status_$proc2_invalid_process_name  0x0019000A

void PROC2_$SET_NAME(char *name, int16_t *name_len, uid_$t *proc_uid, status_$t *status_ret)
{
    int16_t index;
    proc2_info_t *entry;
    status_$t status;
    int16_t len;
    int16_t i;

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(proc_uid, &status);

    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(index);
        len = *name_len;

        /* Validate name length */
        if (len < 0 || len > 32) {
            status = status_$proc2_invalid_process_name;
        }
        else if (len == 0) {
            /* Empty name - set special marker (0x22 = '"' = no name) */
            entry->name_len = 0x22;
        }
        else {
            /* Copy name */
            for (i = 0; i < len; i++) {
                entry->name[i] = name[i];
            }
            entry->name_len = (uint8_t)len;
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
