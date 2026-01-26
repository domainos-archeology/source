/*
 * REM_FILE_$CREATE_TYPE_PRESR10 - Create type (pre-SR10 version) on remote file server
 *
 * Creates a new typed object using the pre-SR10 protocol. This is a two-phase
 * operation similar to CREATE_TYPE but with a simpler request format.
 *
 * Original address: 0x00E61868
 * Size: 270 bytes
 */

#include "rem_file/rem_file_internal.h"

/* proc_priv_table is indexed by PROC1_$CURRENT to check process privileges */
extern int16_t proc_priv_table[];  /* At 0xe7dacc */

/*
 * Create type context structure (passed as param_1)
 */
typedef struct {
    uint32_t reserved[2];       /* Offset 0x00: Reserved */
    uid_t parent_uid;           /* Offset 0x08: Parent UID */
    void *addr_info;            /* Offset 0x10: Address info pointer */
} rem_file_create_type_ctx_t;

/*
 * Create type pre-SR10 phase 1 request structure
 */
typedef struct {
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x24 = Create phase 1 */
    uint8_t padding[14];        /* Padding to 0x10 bytes */
} rem_file_create_presr10_p1_req_t;

/*
 * Create type pre-SR10 phase 2 request structure
 */
typedef struct {
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x26 = Create type pre-SR10 phase 2 */
    uid_t parent_uid;           /* Parent UID (8 bytes) */
    uint16_t flags;             /* Flags */
    uint16_t flags2;            /* Flags 2 (value 3) */
    int8_t priv_flag;           /* Privilege flag */
    uint8_t padding;
    int16_t type_index;         /* Type index (param_3 - 1) */
    uid_t session_uid;          /* Session UID from phase 1 (8 bytes) */
} rem_file_create_presr10_p2_req_t;

/*
 * Create type pre-SR10 response structure
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xB8];
    uid_t session_uid;          /* Session UID */
} rem_file_create_presr10_resp_t;

void REM_FILE_$CREATE_TYPE_PRESR10(void *ctx_ptr, uint16_t flags,
                                    int16_t type_index, uid_t *session_uid_out,
                                    status_$t *status)
{
    rem_file_create_type_ctx_t *ctx = (rem_file_create_type_ctx_t *)ctx_ptr;
    rem_file_create_presr10_p1_req_t req1;
    rem_file_create_presr10_p2_req_t req2;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

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

    /* Phase 2: Send create type request */
    rem_file_create_presr10_resp_t *resp = (rem_file_create_presr10_resp_t *)response;

    req2.magic = 0x80;
    req2.opcode = 0x26;  /* CREATE_TYPE pre-SR10 phase 2 */
    req2.parent_uid = ctx->parent_uid;
    req2.session_uid = resp->session_uid;
    req2.flags = flags;
    req2.flags2 = 3;

    /* Set privilege flag based on process privilege level */
    req2.priv_flag = (proc_priv_table[PROC1_$CURRENT] > 0) ? -1 : 0;
    req2.type_index = type_index - 1;

    /* Copy session UID to output before phase 2 request */
    *session_uid_out = resp->session_uid;

    REM_FILE_$SEND_REQUEST(ctx->addr_info, &req2, 0x1C,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Treat duplicate UID as success */
    if (*status == 0x00020007) {  /* status_$vtoc_duplicate_uid */
        *status = status_$ok;
    }
}
