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
    uint16_t flags;         /* 0x0C: 5                       0x00E62956 */
    uint16_t _pad_0e;       /* 0x0E */
    uint32_t sid_data[9];   /* 0x10: 9 longs from (0x10,A6)  0x00E6295E */
    uint32_t owner_data[3]; /* 0x34: 3 longs from (0x14,A6)  0x00E6296C */
} rem_file_acl_setids_req_t;

/* Request offsets are the A6 displacement plus 0x170 (0x00E6294A-0x00E6297E);
 * total length 0x40 (`move.w #0x40,-(SP)` at 0x00E629AA). */
_Static_assert(__builtin_offsetof(rem_file_acl_setids_req_t, acl_uid) == 0x04, "acl_setids_req.acl_uid");
_Static_assert(__builtin_offsetof(rem_file_acl_setids_req_t, flags) == 0x0C, "acl_setids_req.flags");
_Static_assert(__builtin_offsetof(rem_file_acl_setids_req_t, sid_data) == 0x10, "acl_setids_req.sid_data");
_Static_assert(__builtin_offsetof(rem_file_acl_setids_req_t, owner_data) == 0x34, "acl_setids_req.owner_data");
_Static_assert(sizeof(rem_file_acl_setids_req_t) == 0x40, "acl_setids_req size");

/*
 * ACL setids response structure
 */
typedef struct {
    uint8_t  head[0x0A];    /* 0x00 */
    int8_t   modified_flag; /* 0x0A: "move.b (-0xb6,A6),(A2)"  0x00E629BE */
    uint8_t  _pad_0b;       /* 0x0B */
    uint32_t sid_data[9];   /* 0x0C: "lea (-0xb4,A6),A0" + 9 longs
                             *       (dbf #0x8)          0x00E629C4 */
    uint32_t owner_data[3]; /* 0x30: "lea (-0x90,A6),A0" + 3 longs
                             *       0x00E629D2 */
} rem_file_acl_setids_resp_t;

/* Reply offsets are the A6 displacement plus 0xC0 (buffer at A6-0xC0). */
_Static_assert(__builtin_offsetof(rem_file_acl_setids_resp_t, modified_flag) == 0x0A, "acl_setids_resp.modified_flag");
_Static_assert(__builtin_offsetof(rem_file_acl_setids_resp_t, sid_data) == 0x0C, "acl_setids_resp.sid_data");
_Static_assert(__builtin_offsetof(rem_file_acl_setids_resp_t, owner_data) == 0x30, "acl_setids_resp.owner_data");

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
    request.opcode = REM_FILE_OP_ACL_SETIDS;  /* 0x6A, 0x00E62950 */
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
