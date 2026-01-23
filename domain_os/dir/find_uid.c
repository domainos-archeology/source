/*
 * DIR_$FIND_UID - Find a UID in a directory
 *
 * Searches a directory for an entry with the specified UID and returns
 * its name.
 *
 * Original address: 0x00E4E87C
 * Original size: 56 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$FIND_UID - Find a UID in a directory
 *
 * This function is a wrapper around the internal find function (FUN_00e4e786)
 * with flag=0 to indicate a UID search rather than a network search.
 *
 * Parameters:
 *   dir_uid      - UID of directory to search
 *   target_uid   - UID to find
 *   name_buf_len - Pointer to max buffer length
 *   name_buf     - Output: name of found entry
 *   param5       - Additional parameters (unused in UID search)
 *   status_ret   - Output: status code
 */
void DIR_$FIND_UID(uid_t *dir_uid, uid_t *target_uid, uint16_t *name_buf_len,
                   char *name_buf, void *param5, status_$t *status_ret)
{
    uint32_t dummy_net;  /* Not used for UID search */

    /* Call internal helper with flag=0 for UID search mode */
    FUN_00e4e786(dir_uid, target_uid, 0, (int16_t)*name_buf_len, name_buf,
                 (int16_t *)name_buf_len, &dummy_net, status_ret);
}
