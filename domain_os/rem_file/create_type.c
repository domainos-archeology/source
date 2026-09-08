/*
 * REM_FILE_$CREATE_TYPE - Create typed object on remote file server
 *
 * Creates a new typed object on a remote file server. This is a two-phase
 * operation: first gets a session, then sends the actual create request.
 *
 * Original address: 0x00E6171A
 * Size: 334 bytes
 */

#include "rem_file/rem_file_internal.h"
#include "vtoc/vtoc.h"

/*
 * Create type context structure (passed as param_1)
 */
typedef struct {
    uint32_t reserved[2];       /* 0x00 */
    uid_t    parent_uid;        /* 0x08: "lea (0x8,A2),A0"  0x00E61794 */
    uint32_t addr_info[2];      /* 0x10: the {network, node} address record
                                 *       ITSELF - the sends are handed its
                                 *       address ("pea (0x10,A2)" at
                                 *       0x00E61776 and 0x00E61812) */
} rem_file_create_type_ctx_t;

_Static_assert(__builtin_offsetof(rem_file_create_type_ctx_t, parent_uid) == 0x08, "create_type_ctx.parent_uid");
_Static_assert(__builtin_offsetof(rem_file_create_type_ctx_t, addr_info) == 0x10, "create_type_ctx.addr_info");

/*
 * Create type phase 1 request structure
 */
typedef struct {
    uint16_t msg_type;          /* Set to 1 by SEND_REQUEST */
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x24 = Create phase 1 */
    uint8_t padding[14];        /* Padding to 0x10 bytes */
} rem_file_create_type_p1_req_t;

/*
 * Create type phase 2 request structure
 */
typedef struct {
    uint16_t msg_type;          /* Set to 1 by SEND_REQUEST */
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x7E = Create type phase 2 */
    uid_t parent_uid;           /* Parent UID (8 bytes) */
    uid_t session_uid;          /* Session UID from phase 1 (8 bytes) */
    uid_t type_uid;             /* Type UID (8 bytes) */
    uid_t parent_uid2;          /* Parent UID again (8 bytes) */
    uint32_t type_header[12];   /* Type header (48 bytes) */
    uint32_t extra_data;        /* Extra data */
    uint16_t flags;             /* Flags */
    uint16_t flags2;            /* Flags 2 */
} rem_file_create_type_p2_req_t;

/*
 * Create type response structure
 */
/*
 * The reply buffer.  REM_FILE_$CREATE_TYPE is the only client whose buffer is
 * NOT at A6-0xC0 - it sits at A6-0xE8 - so a field's record offset is its A6
 * displacement plus 0xE8.
 */
typedef union {
    /* Phase 1's reply: the session UID at response+0x08. */
    struct {
        uint8_t  head[0x08];
        uid_t    session_uid;   /* 0x08: "lea (-0xe0,A6),A0"  0x00E617A0 */
    } phase1;
    /* Phase 2's reply.  Its two payloads overlap phase 1's UID. */
    struct {
        uint8_t  head[0x0C];
        uint32_t data_out[36];  /* 0x0C: "lea (-0xdc,A6),A0" + 36 longs
                                 *                            0x00E6184C */
        uint32_t header_out[8]; /* 0x9C: "lea (-0x4c,A6),A0" + 8 longs
                                 *                            0x00E61836 */
        uint16_t tail_bc;       /* 0xBC */
    } phase2;
    uint8_t raw[REM_FILE_RESPONSE_BUF_SIZE];
} rem_file_create_type_resp_t;

_Static_assert(__builtin_offsetof(rem_file_create_type_resp_t, phase1.session_uid) == 0x08, "create_type_resp.session_uid");
_Static_assert(__builtin_offsetof(rem_file_create_type_resp_t, phase2.data_out) == 0x0C, "create_type_resp.data_out");
_Static_assert(__builtin_offsetof(rem_file_create_type_resp_t, phase2.header_out) == 0x9C, "create_type_resp.header_out");

/*
 * Output header structure (8 uint32s = 32 bytes)
 */
typedef struct {
    uint32_t data[8];
} rem_file_create_type_header_out_t;

/*
 * Output data structure (36 uint32s = 144 bytes)
 */
typedef struct {
    uint32_t data[36];
} rem_file_create_type_data_out_t;

void REM_FILE_$CREATE_TYPE(void *ctx_ptr, uint16_t flags, uid_t *type_uid,
                            uint32_t extra_data, uint16_t flags2,
                            void *type_header, void *data_out_ptr,
                            void *header_out_ptr, status_$t *status)
{
    rem_file_create_type_ctx_t *ctx = (rem_file_create_type_ctx_t *)ctx_ptr;
    rem_file_create_type_data_out_t *data_out = (rem_file_create_type_data_out_t *)data_out_ptr;
    rem_file_create_type_header_out_t *header_out = (rem_file_create_type_header_out_t *)header_out_ptr;
    rem_file_create_type_p1_req_t req1;
    rem_file_create_type_p2_req_t req2;
    rem_file_create_type_resp_t response;   /* A6-0xE8 */
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *hdr = (uint32_t *)type_header;

    /* Phase 1: Get session */
    req1.magic = 0x80;
    req1.opcode = REM_FILE_OP_GENERATE_UID;  /* 0x24, 0x00E61742 */

    REM_FILE_$SEND_REQUEST(&ctx->addr_info, &req1, 0x10,
                           &zero, 0,
                           &response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    if (*status != status_$ok) {
        return;
    }

    /* Phase 2: Send create type data */
    req2.magic = 0x80;
    req2.opcode = REM_FILE_OP_CREATE_TYPE;   /* 0x7E, 0x00E61788 */
    req2.parent_uid = ctx->parent_uid;
    req2.session_uid = response.phase1.session_uid;
    req2.type_uid = *type_uid;
    req2.parent_uid2 = ctx->parent_uid;

    /* Copy type header (12 uint32s) */
    for (i = 0; i < 12; i++) {
        req2.type_header[i] = hdr[i];
    }

    req2.extra_data = extra_data;
    req2.flags = flags;
    req2.flags2 = flags2;

    REM_FILE_$SEND_REQUEST(&ctx->addr_info, &req2, 0x5C,
                           &zero, 0,
                           &response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Treat duplicate UID as success */
    /* 0x00E6181E-0x00E61828 */
    if (*status == status_$ok || *status == status_$vtoc_duplicate_uid) {
        /* 0x00E6182A-0x00E61832: the caller's own address record is written
         * back INTO the reply buffer at response+0xAC / +0xB0 - which is
         * header_out[4] and [5] of the block copied out next, so those two
         * longwords survive the copy unchanged. */
        response.phase2.header_out[4] = ctx->addr_info[0];
        response.phase2.header_out[5] = ctx->addr_info[1];

        /* 0x00E61836-0x00E61844: 8 longwords from response+0x9C. */
        for (i = 0; i < 8; i++) {
            header_out->data[i] = response.phase2.header_out[i];
        }

        /* 0x00E61846 */
        ((uint8_t *)header_out)[0x1D] |= 0x80;

        /* 0x00E6184C-0x00E6185C: 36 longwords from response+0x0C. */
        for (i = 0; i < 36; i++) {
            data_out->data[i] = response.phase2.data_out[i];
        }
    }
}
