/*
 * REM_FILE_$ACL_CREATE - Create ACL on remote file
 *
 * Creates a new ACL (Access Control List) on a remote file server.
 * This is a two-phase operation: first gets a session, then sends ACL data.
 *
 * Original address: 0x00E6283C
 * Size: 244 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * ACL create phase 1 request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x24 = ACL create phase 1 */
    uint8_t padding[14];    /* Padding to 0x10 bytes total */
} rem_file_acl_create_p1_req_t;

/*
 * ACL create phase 2 request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x68 = ACL create phase 2 */
    uid_t parent_uid;       /* Parent UID (8 bytes) */
    uint16_t flags;         /* Flags (value 5) */
    uint32_t acl_header[11]; /* ACL header (44 bytes) */
    uid_t session_uid;      /* Session UID from phase 1 (8 bytes) */
} rem_file_acl_create_p2_req_t;

/*
 * ACL create response structure
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xB8];
    uid_t session_uid;      /* Session UID (phase 1) */
    uid_t acl_uid;          /* ACL UID (phase 2) */
} rem_file_acl_create_resp_t;

void REM_FILE_$ACL_CREATE(void *addr_info, void *acl_data,
                          void *acl_header, uid_t *parent_uid,
                          uid_t *acl_uid_out, status_$t *status)
{
    rem_file_acl_create_p1_req_t req1;
    rem_file_acl_create_p2_req_t req2;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;

    /* Phase 1: Get session */
    req1.magic = 0x80;
    req1.opcode = 0x24;  /* ACL_CREATE phase 1 */

    REM_FILE_$SEND_REQUEST(addr_info, &req1, 0x10,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    if (*status != status_$ok) {
        return;
    }

    /* Phase 2: Send ACL data */
    rem_file_acl_create_resp_t *resp = (rem_file_acl_create_resp_t *)response;

    req2.magic = 0x80;
    req2.opcode = 0x68;  /* ACL_CREATE phase 2 */
    req2.parent_uid = *parent_uid;
    req2.flags = 5;

    /* Copy ACL header (11 uint32s) */
    uint32_t *hdr = (uint32_t *)acl_header;
    for (i = 0; i < 11; i++) {
        req2.acl_header[i] = hdr[i];
    }

    /* Copy session UID from phase 1 response */
    req2.session_uid = resp->session_uid;

    /* Send with bulk ACL data (up to 1KB) */
    REM_FILE_$SEND_REQUEST(addr_info, &req2, 0x48,
                           acl_data, 0x400,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Treat duplicate UID as success */
    if (*status == 0x00020007) {  /* status_$vtoc_duplicate_uid */
        *status = status_$ok;
    }

    /* Copy ACL UID from response */
    resp = (rem_file_acl_create_resp_t *)response;
    *acl_uid_out = resp->acl_uid;
}
