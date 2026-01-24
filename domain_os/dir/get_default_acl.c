/*
 * DIR_$GET_DEFAULT_ACL - Get default ACL for a directory
 *
 * Retrieves the default ACL UID for the directory.
 *
 * Original address: 0x00E531C8
 * Original size: 150 bytes
 */

#include "dir/dir_internal.h"

/*
 * Request structure for GET_DEFAULT_ACL operation
 */
typedef struct {
    uint8_t   op;           /* Operation code: DIR_OP_GET_DEFAULT_ACL (0x4E) */
    uint8_t   padding[3];
    uid_t     dir_uid;      /* Directory UID */
    uint16_t  reserved;     /* Reserved field */
    uint8_t   gap[0x80];    /* Gap to acl_type position */
    uid_t     acl_type;     /* ACL type UID */
} Dir_$GetDefaultAclRequest;

/*
 * DIR_$GET_DEFAULT_ACL - Get default ACL for a directory
 *
 * Sends a GET_DEFAULT_ACL request to retrieve the default ACL UID
 * for the specified directory and ACL type.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   acl_ret    - Output: default ACL UID
 *   status_ret - Output: status code
 */
void DIR_$GET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                          status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uid_t     type;
    } request;
    Dir_$OpResponse response;

    /* Build the request */
    request.op = DIR_OP_GET_DEFAULT_ACL;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fcd2;
    request.type.high = acl_type->high;
    request.type.low = acl_type->low;

    /* Send the request */
    DIR_$DO_OP(&request.op, DAT_00e7fcd6, 0x1c, &response, &request);

    /* Check for fallback conditions */
    if (response.status == file_$bad_reply_received_from_remote_node ||
        response.status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_GET_DEFAULT_ACL(dir_uid, acl_type, acl_ret, status_ret);
    } else {
        /* Extract result from response */
        acl_ret->high = response._22_4_;
        acl_ret->low = response._24_4_;
        *status_ret = response.status;
    }
}
