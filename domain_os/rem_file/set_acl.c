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
    uint16_t msg_type;          /* Set to 1 by SEND_REQUEST */
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x66 = Set ACL */
    uid_t file_uid;             /* File UID (8 bytes) */
    uint16_t flags;             /* Flags (value 5) */
    uint16_t flags2;            /* Additional flags */
    uint32_t sid_data[9];       /* SID data (36 bytes) */
    uint32_t perm_data[16];     /* Permission data (64 bytes) */
    uid_t acl_uid;              /* ACL UID (8 bytes) */
    uint32_t acl_header[11];    /* ACL header (44 bytes) */
    uint16_t extra_flags;       /* 0xA8: D0 = (0x22,A6)   0x00E62B1E */
} rem_file_set_acl_req_t;

/*
 * Request offsets are the A6 displacement plus 0x170; verified against
 * 0x00E62ABE-0x00E62B1E, total length 0xAA (`move.w #0xaa,-(SP)` at
 * 0x00E62B4A).
 */
_Static_assert(__builtin_offsetof(rem_file_set_acl_req_t, file_uid) == 0x04, "set_acl_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_set_acl_req_t, flags) == 0x0C, "set_acl_req.flags");
_Static_assert(__builtin_offsetof(rem_file_set_acl_req_t, flags2) == 0x0E, "set_acl_req.flags2");
_Static_assert(__builtin_offsetof(rem_file_set_acl_req_t, sid_data) == 0x10, "set_acl_req.sid_data");
_Static_assert(__builtin_offsetof(rem_file_set_acl_req_t, perm_data) == 0x34, "set_acl_req.perm_data");
_Static_assert(__builtin_offsetof(rem_file_set_acl_req_t, acl_uid) == 0x74, "set_acl_req.acl_uid");
_Static_assert(__builtin_offsetof(rem_file_set_acl_req_t, acl_header) == 0x7C, "set_acl_req.acl_header");
_Static_assert(__builtin_offsetof(rem_file_set_acl_req_t, extra_flags) == 0xA8, "set_acl_req.extra_flags");
_Static_assert(sizeof(rem_file_set_acl_req_t) == 0xAA, "set_acl_req size");

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
    request.opcode = REM_FILE_OP_SET_ACL;  /* 0x66, 0x00E62AC4 */
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
