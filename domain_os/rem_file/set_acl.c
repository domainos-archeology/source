/*
 * REM_FILE_$SET_ACL - Set ACL on remote file
 *
 * Sets the ACL (Access Control List) on a remote file.
 *
 * Original address: 0x00E62AA8
 * Size: 188 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Set ACL request structure
 */
typedef struct {
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x66 = Set ACL */
    uid_t file_uid;             /* File UID (8 bytes) */
    uint16_t flags;             /* Flags (value 5) */
    uint16_t flags2;            /* Additional flags */
    uint32_t sid_data[9];       /* SID data (36 bytes) */
    uint32_t perm_data[16];     /* Permission data (64 bytes) */
    uid_t acl_uid;              /* ACL UID (8 bytes) */
    uint32_t acl_header[11];    /* ACL header (44 bytes) */
    uint16_t extra_flags;       /* Extra flags */
} rem_file_set_acl_req_t;

void REM_FILE_$SET_ACL(void *addr_info, uid_t *file_uid, uid_t *acl_uid,
                       void *acl_header, void *sid_data, void *perm_data,
                       uint16_t flags2, uint16_t extra_flags,
                       status_$t *status)
{
    rem_file_set_acl_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *sid = (uint32_t *)sid_data;
    uint32_t *perm = (uint32_t *)perm_data;
    uint32_t *hdr = (uint32_t *)acl_header;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x66;  /* SET_ACL opcode */
    request.file_uid = *file_uid;
    request.flags = 5;
    request.flags2 = flags2;

    /* Copy SID data (9 uint32s) */
    for (i = 0; i < 9; i++) {
        request.sid_data[i] = sid[i];
    }

    /* Copy permission data (16 uint32s) */
    for (i = 0; i < 16; i++) {
        request.perm_data[i] = perm[i];
    }

    request.acl_uid = *acl_uid;

    /* Copy ACL header (11 uint32s) */
    for (i = 0; i < 11; i++) {
        request.acl_header[i] = hdr[i];
    }

    request.extra_flags = extra_flags;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0xAA,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
