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

/*
 * Create type context structure (passed as param_1)
 */
typedef struct {
    uint32_t reserved[2];       /* Offset 0x00: Reserved */
    uid_t parent_uid;           /* Offset 0x08: Parent UID */
    void *addr_info;            /* Offset 0x10: Address info pointer */
} rem_file_create_type_ctx_t;

/*
 * Create type phase 1 request structure
 */
typedef struct {
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x24 = Create phase 1 */
    uint8_t padding[14];        /* Padding to 0x10 bytes */
} rem_file_create_type_p1_req_t;

/*
 * Create type phase 2 request structure
 */
typedef struct {
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
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xE4];
    uid_t session_uid;          /* Session UID (from phase 1) */
    uint8_t phase2_data[0x90];  /* Phase 2 response data */
} rem_file_create_type_resp_t;

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
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *hdr = (uint32_t *)type_header;

    /* Phase 1: Get session */
    req1.magic = 0x80;
    req1.opcode = 0x24;  /* CREATE phase 1 */

    REM_FILE_$SEND_REQUEST(ctx->addr_info, &req1, 0x10,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    if (*status != status_$ok) {
        return;
    }

    /* Phase 2: Send create type data */
    rem_file_create_type_resp_t *resp = (rem_file_create_type_resp_t *)response;

    req2.magic = 0x80;
    req2.opcode = 0x7E;  /* CREATE_TYPE phase 2 */
    req2.parent_uid = ctx->parent_uid;
    req2.session_uid = resp->session_uid;
    req2.type_uid = *type_uid;
    req2.parent_uid2 = ctx->parent_uid;

    /* Copy type header (12 uint32s) */
    for (i = 0; i < 12; i++) {
        req2.type_header[i] = hdr[i];
    }

    req2.extra_data = extra_data;
    req2.flags = flags;
    req2.flags2 = flags2;

    REM_FILE_$SEND_REQUEST(ctx->addr_info, &req2, 0x5C,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Treat duplicate UID as success */
    if (*status == status_$ok || *status == 0x00020007) {  /* status_$vtoc_duplicate_uid */
        /* Copy address info to header output */
        uint32_t *addr_info_words = (uint32_t *)ctx->addr_info;
        header_out->data[6] = addr_info_words[0];
        header_out->data[7] = addr_info_words[1];

        /* Copy response header data (8 uint32s from offset -0x4C) */
        resp = (rem_file_create_type_resp_t *)response;
        uint32_t *resp_header = (uint32_t *)(response + REM_FILE_RESPONSE_BUF_SIZE - 0x50);
        for (i = 0; i < 8; i++) {
            header_out->data[i] = resp_header[i];
        }

        /* Set high bit of byte at offset 0x1D */
        ((uint8_t *)header_out)[0x1D] |= 0x80;

        /* Copy response data (36 uint32s from offset -0xDC) */
        uint32_t *resp_data = (uint32_t *)(response + REM_FILE_RESPONSE_BUF_SIZE - 0xE0);
        for (i = 0; i < 36; i++) {
            data_out->data[i] = resp_data[i];
        }
    }
}
