/*
 * DIR_$OLD_VALIDATE_ROOT_ENTRY - Legacy validate root directory entry
 *
 * Validates a root directory entry by comparing the local entry
 * against the replicated entry from the naming server.
 *
 * Original address: 0x00E580C4
 * Original size: 314 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_VALIDATE_ROOT_ENTRY - Legacy validate root directory entry
 *
 * The process is:
 * 1. Look up the entry locally via FUN_00e57ce0
 * 2. Look up the entry from the naming server via REM_NAME_$GET_ENTRY
 * 3. Compare the two entries
 * 4. If they differ:
 *    a. Fix stale entries via FUN_00e56a04
 *    b. Re-add via FUN_00e56682 if needed
 *    c. Return status_$naming_entry_repaired or status_$naming_entry_stale
 * 5. If they match, return status_$ok
 *
 * Parameters:
 *   name       - Name to validate
 *   name_len   - Pointer to name length
 *   status_ret - Output: status code
 */
void DIR_$OLD_VALIDATE_ROOT_ENTRY(char *name, uint16_t *name_len,
                                  status_$t *status_ret)
{
    uid_t root_uid;
    uint8_t local_entry[64];    /* Local entry data */
    uint8_t remote_entry[64];   /* Remote entry data */
    status_$t remote_status;

    /* Get root UID */
    root_uid.high = NAME_$ROOT_UID.high;
    root_uid.low = NAME_$ROOT_UID.low;

    /* Look up entry locally */
    FUN_00e57ce0(&root_uid, name, *name_len, local_entry, status_ret);
    if ((int16_t)*status_ret != 0) {
        return;
    }

    /* Look up entry from naming server */
    REM_NAME_$GET_ENTRY(&root_uid, name, name_len, remote_entry, &remote_status);
    if (remote_status != status_$ok) {
        /* If remote lookup fails, check if local entry is stale */
        if (remote_status == status_$naming_name_not_found) {
            /* Entry exists locally but not on server - mark as stale */
            *status_ret = status_$naming_entry_stale;
        } else {
            *status_ret = remote_status;
        }
        return;
    }

    /* Compare local and remote entries (compare the UID at a known offset) */
    /* Entry structure: offset 0x02-0x05 = UID high, 0x06-0x09 = UID low */
    if (*((uint32_t *)(local_entry + 2)) != *((uint32_t *)(remote_entry + 2)) ||
        *((uint32_t *)(local_entry + 6)) != *((uint32_t *)(remote_entry + 6))) {
        /* Entries differ - fix the local entry */
        FUN_00e56a04(&root_uid, name, *name_len, remote_entry);

        /* Re-add the entry from remote data */
        FUN_00e56682(&root_uid, 2, name, *name_len,
                     (uid_t *)(remote_entry + 2), 0, status_ret);

        if ((int16_t)*status_ret == 0) {
            *status_ret = status_$naming_entry_repaired;
        }
    } else {
        *status_ret = status_$ok;
    }
}
