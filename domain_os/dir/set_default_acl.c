/*
 * DIR_$SET_DEFAULT_ACL - Set default ACL for a directory
 *
 * Sets the default ACL for new entries in the directory.
 *
 * Original address: 0x00E53004
 * Original size: 292 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$SET_DEFAULT_ACL - Set default ACL for a directory
 *
 * Sets the default ACL that will be applied to new entries created
 * in the directory. Handles conversion between ACL formats.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   acl_uid    - ACL UID to set
 *   status_ret - Output: status code
 */
void DIR_$SET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_uid,
                          status_$t *status_ret)
{
    uid_t local_dir, local_acl;
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uid_t     type;         /* ACL type */
        uid_t     acl;          /* ACL UID */
    } request;
    Dir_$OpResponse response;
    status_$t status;
    uid_t result_acl;
    uint8_t temp_buf[8];

    /* Make local copies of UIDs */
    local_dir.high = dir_uid->high;
    local_dir.low = dir_uid->low;
    local_acl.high = acl_uid->high;
    local_acl.low = acl_uid->low;

    /* Build the request */
    request.op = DIR_OP_SET_DEFAULT_ACL;
    request.uid.high = local_dir.high;
    request.uid.low = local_dir.low;
    request.reserved = DAT_00e7fcca;
    request.type.high = acl_type->high;
    request.type.low = acl_type->low;
    request.acl.high = local_acl.high;
    request.acl.low = local_acl.low;

    /* Send the request */
    DIR_$DO_OP(&request.op, DAT_00e7fcce, 0x14, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == status_$file_bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {

        result_acl.high = local_acl.high;
        result_acl.low = local_acl.low;

        /* Check if ACL has "funky" bits set (bits 5-7 of byte 5) */
        if (((local_acl.low >> 4) & 0xE0) != 0) {
            /* Convert funky ACL format */
            ACL_$CONVERT_FUNKY_ACL(&local_acl, &response.f18, &result_acl,
                                   temp_buf, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }
        }

        /* Check if ACL has the "9ACL" bit set (bit 4 of byte 5) */
        if (((result_acl.low >> 4) & 0x10) == 0) {
            /* Convert to 9ACL format */
            ACL_$CONVERT_TO_9ACL((int16_t)(uintptr_t)&response.f18, &result_acl,
                                 &local_dir, acl_type, &result_acl, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }
        } else {
            /* Clear the "new format" bit */
            result_acl.low &= 0xFEFFFFFF;
        }

        /* Call old implementation */
        DIR_$OLD_SET_DEFAULT_ACL(&local_dir, acl_type, &result_acl, status_ret);
    } else {
        *status_ret = status;
    }
}
