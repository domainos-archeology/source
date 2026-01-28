/*
 * REM_FILE_$SET_DEF_ACL - Set default ACL on remote directory
 *
 * Sets the default ACL for a directory on a remote file server.
 * New files created in the directory will inherit this ACL.
 *
 * Original address: 0x00E622EE
 * Size: 134 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Set default ACL request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x18 = set default ACL */
    uid_t dir_uid;          /* Directory UID (8 bytes) */
    uid_t acl_uid;          /* ACL UID (8 bytes) */
    uid_t owner_uid;        /* Owner UID (8 bytes) */
    uint16_t flags;         /* Flags (value 3) */
    uint8_t force_flag;     /* Force flag (0xFF) */
} rem_file_set_def_acl_req_t;

void REM_FILE_$SET_DEF_ACL(void *vol_uid, uid_t *dir_uid, uid_t *acl_uid,
                           uid_t *owner_uid, status_$t *status)
{
    rem_file_set_def_acl_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x18;  /* SET_DEF_ACL opcode */
    request.dir_uid = *dir_uid;
    request.acl_uid = *acl_uid;
    request.owner_uid = *owner_uid;
    request.flags = 3;
    request.force_flag = 0xFF;

    /* Send request */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, 0x20,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
