/*
 * REM_FILE_$ACL_CHECK_RIGHTS - Check ACL rights on remote file
 *
 * Checks if specified access rights are granted by the ACL on a remote file.
 *
 * Original address: 0x00E629E8
 * Size: 192 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * ACL check rights request structure
 */
typedef struct {
    uint16_t msg_type;          /* Set to 1 by SEND_REQUEST */
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x6C = ACL check rights */
    uid_t file_uid;             /* File UID (8 bytes) */
    uint16_t flags;             /* Flags (value 5) */
    uint16_t flags2;            /* Additional flags */
    uint32_t sid_data[9];       /* SID data (36 bytes) */
    uint32_t perm_data[16];     /* Permission data (64 bytes) */
    uint32_t access_mask;       /* Access mask to check */
    uint8_t check_flag;         /* Check flag */
    uint8_t flag2;              /* Flag 2 */
    uint8_t flag3;              /* Flag 3 */
    uint8_t padding;            /* Padding to align */
} rem_file_acl_check_rights_req_t;

/*
 * ACL check rights response structure
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xB4];
    uint32_t result;            /* Check result */
} rem_file_acl_check_rights_resp_t;

void REM_FILE_$ACL_CHECK_RIGHTS(void *addr_info, void *sid_data,
                                 void *perm_data, uid_t *file_uid,
                                 uint8_t check_flag, uint32_t access_mask,
                                 uint16_t flags2, uint8_t flag2, uint8_t flag3,
                                 uint32_t *result_out, status_$t *status)
{
    rem_file_acl_check_rights_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *sid = (uint32_t *)sid_data;
    uint32_t *perm = (uint32_t *)perm_data;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x6C;  /* ACL_CHECK_RIGHTS opcode */
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

    request.access_mask = access_mask;
    request.check_flag = check_flag;
    request.flag2 = flag2;
    request.flag3 = flag3;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x7C,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Extract result from response */
    rem_file_acl_check_rights_resp_t *resp = (rem_file_acl_check_rights_resp_t *)response;
    *result_out = resp->result;
}
