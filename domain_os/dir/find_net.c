/*
 * DIR_$FIND_NET - Find network node for a directory entry
 *
 * Returns the high part of the network node address for the given index.
 *
 * Original address: 0x00E4E8B4
 * Original size: 86 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$FIND_NET - Find network node for a directory entry
 *
 * Creates a local UID with the low word masked to include only
 * the top 12 bits OR'd with the index value (low 20 bits).
 * Then calls FUN_00e4e786 with flag=0xFF for network search mode.
 *
 * Parameters:
 *   dir_uid - UID of directory
 *   index   - Pointer to index value (low 20 bits of UID)
 *
 * Returns:
 *   High part of node address, or 0 on failure
 */
uint32_t DIR_$FIND_NET(uid_t *dir_uid, uint32_t *index)
{
    uid_t local_uid;
    char name_buf[256];
    int16_t name_len;
    uint32_t net_ret;
    status_$t status;

    /* Copy directory UID and mask in the index */
    local_uid.high = dir_uid->high;
    local_uid.low = (dir_uid->low & 0xFFF00000) | (*index & 0x000FFFFF);

    /* Call internal helper with flag=0xFF for network search mode */
    FUN_00e4e786(dir_uid, &local_uid, (int8_t)0xFF, 0, name_buf,
                 &name_len, &net_ret, &status);

    /* Return network address if successful, 0 otherwise */
    if (status == status_$ok) {
        return net_ret;
    }
    return 0;
}
