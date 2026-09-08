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
#include "vtoc/vtoc.h"

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
    uid_t parent_uid;        /* 0x04: (0x14,A6)   0x00E628AC */
    uint16_t flags;          /* 0x0C: 5           0x00E628C6 */
    uint16_t _pad_0e;        /* 0x0E */
    uint32_t acl_header[11]; /* 0x10: 11 longs from (0x10,A6)  0x00E628B8 */
    uint32_t _pad_3c;        /* 0x3C: never written */
    uid_t session_uid;       /* 0x40: response+0x08 of phase 1  0x00E628CC */
} rem_file_acl_create_p2_req_t;

/* Request offsets are the A6 displacement plus 0x170. */
_Static_assert(__builtin_offsetof(rem_file_acl_create_p2_req_t, parent_uid) == 0x04, "acl_create_p2.parent_uid");
_Static_assert(__builtin_offsetof(rem_file_acl_create_p2_req_t, flags) == 0x0C, "acl_create_p2.flags");
_Static_assert(__builtin_offsetof(rem_file_acl_create_p2_req_t, acl_header) == 0x10, "acl_create_p2.acl_header");
_Static_assert(__builtin_offsetof(rem_file_acl_create_p2_req_t, session_uid) == 0x40, "acl_create_p2.session_uid");
_Static_assert(sizeof(rem_file_acl_create_p2_req_t) == 0x48, "acl_create_p2 size");

/*
 * ACL create response structure
 */
/*
 * Both phases reuse the one 0xBE-byte buffer at A6-0xC0, and their payloads
 * overlap: phase 1's session UID is at response+0x08 ("lea (-0xb8,A6),A0" at
 * 0x00E628CC) and phase 2's ACL UID at response+0x0C ("lea (-0xb4,A6),A0" at
 * 0x00E62918).
 */
typedef union {
    struct {
        uint8_t head[0x08];
        uid_t   session_uid;    /* 0x08 */
    } phase1;
    struct {
        uint8_t head[0x0C];
        uid_t   acl_uid;        /* 0x0C */
    } phase2;
    uint8_t raw[REM_FILE_RESPONSE_BUF_SIZE];
} rem_file_acl_create_resp_t;

_Static_assert(__builtin_offsetof(rem_file_acl_create_resp_t, phase1.session_uid) == 0x08, "acl_create_resp.session_uid");
_Static_assert(__builtin_offsetof(rem_file_acl_create_resp_t, phase2.acl_uid) == 0x0C, "acl_create_resp.acl_uid");

void REM_FILE_$ACL_CREATE(void *addr_info, void *acl_data,
                          void *acl_header, uid_t *parent_uid,
                          uid_t *acl_uid_out, status_$t *status)
{
    rem_file_acl_create_p1_req_t req1;
    rem_file_acl_create_p2_req_t req2;
    rem_file_acl_create_resp_t response;    /* A6-0xC0 */
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;

    /* Phase 1: Get session */
    req1.magic = 0x80;
    req1.opcode = REM_FILE_OP_GENERATE_UID;   /* 0x24, 0x00E62858 */

    REM_FILE_$SEND_REQUEST(addr_info, &req1, 0x10,
                           &zero, 0,
                           &response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    if (*status != status_$ok) {
        return;
    }

    /* Phase 2: Send ACL data */

    req2.magic = 0x80;
    req2.opcode = REM_FILE_OP_ACL_CREATE;     /* 0x68, 0x00E628A2 */
    req2.parent_uid = *parent_uid;
    req2.flags = 5;

    /* Copy ACL header (11 uint32s) */
    uint32_t *hdr = (uint32_t *)acl_header;
    for (i = 0; i < 11; i++) {
        req2.acl_header[i] = hdr[i];
    }

    /* Copy session UID from phase 1 response */
    req2.session_uid = response.phase1.session_uid;

    /* Send with bulk ACL data (up to 1KB) */
    REM_FILE_$SEND_REQUEST(addr_info, &req2, 0x48,
                           acl_data, 0x400,
                           &response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Treat duplicate UID as success */
    if (*status == status_$vtoc_duplicate_uid) {   /* 0x00E6290E */
        *status = status_$ok;
    }

    /* Copy ACL UID from response */
    *acl_uid_out = response.phase2.acl_uid;
}
