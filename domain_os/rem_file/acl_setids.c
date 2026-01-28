/*
 * REM_FILE_$ACL_SETIDS - Set IDs in remote ACL
 *
 * Updates the subject IDs (SIDs) in an ACL on a remote file server.
 *
 * Original address: 0x00E62930
 * Size: 184 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * ACL setids request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x6A = ACL setids */
    uid_t acl_uid;          /* ACL UID (8 bytes) */
    uint16_t flags;         /* Flags (value 5) */
    uint32_t sid_data[9];   /* SID data (36 bytes) */
    uint32_t owner_data[3]; /* Owner data (12 bytes) */
} rem_file_acl_setids_req_t;

/*
 * ACL setids response structure
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xB6];
    int8_t modified_flag;   /* Flag indicating if data was modified */
    uint8_t padding2;
    uint32_t sid_data[9];   /* Updated SID data */
    uint8_t padding3[0x94 - 0xB4 + REM_FILE_RESPONSE_BUF_SIZE - 0x24];
    uint32_t owner_data[3]; /* Updated owner data */
} rem_file_acl_setids_resp_t;

void REM_FILE_$ACL_SETIDS(void *addr_info, uid_t *acl_uid,
                          void *sid_data, void *owner_data,
                          int8_t *modified_flag_out, status_$t *status)
{
    rem_file_acl_setids_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *sid = (uint32_t *)sid_data;
    uint32_t *owner = (uint32_t *)owner_data;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x6A;  /* ACL_SETIDS opcode */
    request.acl_uid = *acl_uid;
    request.flags = 5;

    /* Copy SID data (9 uint32s) */
    for (i = 0; i < 9; i++) {
        request.sid_data[i] = sid[i];
    }

    /* Copy owner data (3 uint32s) */
    for (i = 0; i < 3; i++) {
        request.owner_data[i] = owner[i];
    }

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x40,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Extract modified flag from response */
    rem_file_acl_setids_resp_t *resp = (rem_file_acl_setids_resp_t *)response;
    *modified_flag_out = resp->modified_flag;

    /* If modified flag is set (negative), update SID and owner data */
    if (resp->modified_flag < 0) {
        /* Copy updated SID data back (9 uint32s) */
        for (i = 0; i < 9; i++) {
            sid[i] = resp->sid_data[i];
        }

        /* Copy updated owner data back (3 uint32s) */
        for (i = 0; i < 3; i++) {
            owner[i] = resp->owner_data[i];
        }
    }
}
