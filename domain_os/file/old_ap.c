/*
 * FILE_$OLD_AP - Set file access protection (old/legacy interface)
 *
 * Legacy function for setting file access protection.
 * Wraps FILE_$SET_PROT_INT with different parameter handling.
 *
 * Original address: 0x00E5E100
 */

#include "file/file_internal.h"

/*
 * FILE_$OLD_AP
 *
 * Legacy interface for setting file access protection.
 * Used for backward compatibility with older protection schemes.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   prot_type  - Pointer to protection type
 *   acl_data   - ACL data buffer (44 bytes)
 *   acl_uid    - ACL UID (8 bytes)
 *   status_ret - Output status code
 *
 * Protection types:
 *   6: Uses ACL UID directly (copies to first entry of ACL data)
 *   Other: Uses attribute ID 0x13
 *
 * Flow:
 * 1. If type is 6, copy ACL UID to ACL data and use attr_id 0x03
 * 2. Otherwise, copy all ACL data and use attr_id 0x13
 * 3. Call FILE_$SET_PROT_INT with subsys_flag = -1
 */
void FILE_$OLD_AP(uid_t *file_uid, int16_t *prot_type, uint32_t *acl_data,
                  uint32_t *acl_uid, status_$t *status_ret)
{
    uint16_t attr_id;
    int16_t i;
    uint32_t *src;
    uint32_t *dst;

    /* Local ACL data buffer (11 uint32_t = 44 bytes) */
    uint32_t local_acl[11];

    /* Local UID buffer (2 uint32_t = 8 bytes) */
    uint32_t local_uid[2];

    int16_t type_val = *prot_type;

    if (type_val == 6) {
        /*
         * Type 6: Set protection by ACL UID.
         * Copy ACL UID to first two entries of local ACL.
         */
        local_acl[0] = acl_uid[0];
        local_acl[1] = acl_uid[1];

        /* Also copy to local UID for consistency */
        local_uid[0] = acl_uid[0];
        local_uid[1] = acl_uid[1];

        attr_id = 0x03;
    } else {
        /*
         * Other types: Copy all ACL data.
         * Uses attribute ID 0x13.
         */
        src = acl_data;
        dst = local_acl;
        for (i = 10; i >= 0; i--) {
            *dst++ = *src++;
        }

        /* Copy ACL UID */
        local_uid[0] = acl_uid[0];
        local_uid[1] = acl_uid[1];

        attr_id = 0x13;
    }

    /*
     * Call internal set protection.
     * The subsys_flag is -1 (0xFF), indicating this is an old-style call
     * that may need subsystem data override permissions.
     */
    FILE_$SET_PROT_INT(file_uid, local_acl, attr_id, type_val, -1, status_ret);
}
