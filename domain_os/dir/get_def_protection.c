/*
 * DIR_$GET_DEF_PROTECTION - Get default protection for a directory
 *
 * Retrieves the default ACL/protection that will be applied to new
 * entries created in this directory.
 *
 * Original address: 0x00E51D54
 * Original size: 196 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$GET_DEF_PROTECTION - Get default protection for a directory
 *
 * Retrieves the default protection settings for a directory. These
 * settings are applied to new files created in the directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   prot_buf   - Output: protection data (44 bytes, 11 uint32_t values)
 *   prot_uid   - Output: protection UID
 *   status_ret - Output: status code
 */
void DIR_$GET_DEF_PROTECTION(uid_t *dir_uid, uid_t *acl_type,
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
    } request;
    struct {
        uint8_t   flags[20];    /* Response header */
        uint32_t  prot[11];     /* Protection data (44 bytes) */
        uid_t     prot_id;      /* Protection UID */
    } response;
    status_$t status;
    uint32_t *src, *dst;
    int16_t i;

    /* Build the request */
    request.op = DIR_OP_GET_DEF_PROTECTION;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fcf2;
    request.type.high = acl_type->high;
    request.type.low = acl_type->low;

    /* Send the request */
    DIR_$DO_OP(&request.op, DAT_00e7fcf6, 0x48,
               (Dir_$OpResponse *)response.flags, &request);

    status = *((status_$t *)&response.flags[4]);

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation, then convert ACL format */
        uid_t temp_acl;
        DIR_$OLD_GET_DEFAULT_ACL(dir_uid, acl_type, &temp_acl, status_ret);
        if (*status_ret == status_$ok) {
            /* Convert from old 9ACL format to new format */
            ACL_$CONVERT_FROM_9ACL(&temp_acl, acl_type, prot_buf, prot_uid, status_ret);
        }
    } else {
        /* Copy protection data from response */
        src = response.prot;
        dst = (uint32_t *)prot_buf;
        for (i = 0; i < 11; i++) {
            *dst++ = *src++;
        }
        /* Copy protection UID */
        prot_uid->high = response.prot_id.high;
        prot_uid->low = response.prot_id.low;
        *status_ret = status;
    }
}
