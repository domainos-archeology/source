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
    uint8_t padding;            /* 0x7B */
} rem_file_acl_check_rights_req_t;

/* Request offsets are the A6 displacement plus 0x170 (0x00E62A0A-0x00E62A5E);
 * total length 0x7C (`move.w #0x7c,-(SP)` at 0x00E62A86). */
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, file_uid) == 0x04, "acl_check_rights_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, flags) == 0x0C, "acl_check_rights_req.flags");
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, flags2) == 0x0E, "acl_check_rights_req.flags2");
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, sid_data) == 0x10, "acl_check_rights_req.sid_data");
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, perm_data) == 0x34, "acl_check_rights_req.perm_data");
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, access_mask) == 0x74, "acl_check_rights_req.access_mask");
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, check_flag) == 0x78, "acl_check_rights_req.check_flag");
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, flag2) == 0x79, "acl_check_rights_req.flag2");
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_req_t, flag3) == 0x7A, "acl_check_rights_req.flag3");
_Static_assert(sizeof(rem_file_acl_check_rights_req_t) == 0x7C, "acl_check_rights_req size");

/*
 * ACL check rights response structure
 */
typedef struct {
    uint8_t  head[0x0C];        /* 0x00 */
    uint32_t result;            /* 0x0C: "move.l (-0xb4,A6),(A0)" 0x00E62A9A */
} rem_file_acl_check_rights_resp_t;

/* Reply offsets are the A6 displacement plus 0xC0 (buffer at A6-0xC0). */
_Static_assert(__builtin_offsetof(rem_file_acl_check_rights_resp_t, result) == 0x0C,
               "acl_check_rights_resp.result");

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
    request.opcode = REM_FILE_OP_ACL_CHECK_RIGHTS;  /* 0x6C, 0x00E62A10 */
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
