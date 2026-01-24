/*
 * DIR_$SET_DEF_PROTECTION - Set default protection for a directory
 *
 * Sets the default ACL/protection that will be applied to new
 * entries created in this directory.
 *
 * Original address: 0x00E520A6
 * Original size: 196 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$SET_DEF_PROTECTION - Set default protection for a directory
 *
 * Sets the default protection settings for a directory. These
 * settings will be applied to new files created in the directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   prot_buf   - Protection data to set (44 bytes, 11 uint32_t values)
 *   prot_uid   - Protection UID
 *   status_ret - Output: status code
 */
void DIR_$SET_DEF_PROTECTION(uid_t *dir_uid, uid_t *acl_type,
                             void *prot_buf, uid_t *prot_uid,
                             status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uid_t     type;         /* ACL type */
        uint32_t  prot[11];     /* Protection data (44 bytes) */
        uid_t     prot_id;      /* Protection UID */
    } request;
    Dir_$OpResponse response;
    status_$t status;
    uint32_t *src, *dst;
    int16_t i;

    /* Build the request */
    request.op = DIR_OP_SET_DEF_PROTECTION;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fcea;
    request.type.high = acl_type->high;
    request.type.low = acl_type->low;

    /* Copy protection data into request */
    src = (uint32_t *)prot_buf;
    dst = request.prot;
    for (i = 0; i < 11; i++) {
        *dst++ = *src++;
    }

    /* Copy protection UID */
    request.prot_id.high = prot_uid->high;
    request.prot_id.low = prot_uid->low;

    /* Send the request */
    DIR_$DO_OP(&request.op, DAT_00e7fcee, 0x14, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Convert to old 9ACL format and use old implementation */
        uid_t temp_acl;
        ACL_$CONVERT_TO_9ACL((int16_t)(uintptr_t)prot_buf, prot_uid, dir_uid,
                             acl_type, &temp_acl, status_ret);
        if (*status_ret == status_$ok) {
            DIR_$OLD_SET_DEFAULT_ACL(dir_uid, acl_type, &temp_acl, status_ret);
        }
    } else {
        *status_ret = status;
    }
}
