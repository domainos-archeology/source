/*
 * REM_FILE_$RN_DO_OP - Execute remote naming operation
 *
 * General-purpose remote operation dispatcher. Handles different operation
 * types ('X', 'B', '<', '>') with varying bulk data configurations.
 *
 * Original address: 0x00E61538
 * Size: 480 bytes
 */

#include "rem_file/rem_file_internal.h"

/* Forward declarations for ACL functions */
extern void ACL_$GET_RE_ALL_SIDS(void *re_sids, void *sids1, void *out1, void *sids2, status_$t *status);
extern void ACL_$GET_PROJ_LIST(void *proj_list, void *default_proj, void *proj_out, status_$t *status);
extern int8_t ACL_$IN_SUBSYS(void);

/* Forward declaration for data copy */
extern void OS_$DATA_COPY(void *src, void *dst, uint32_t len);

/* Default project UID (static data at 0xe61718) */
static const uid_t default_proj_uid = {0};

/*
 * Remote operation request buffer structure
 * The actual layout depends on the operation type (byte at offset 3)
 */
typedef struct {
    uint8_t reserved1;          /* Offset 0 */
    uint8_t reserved2;          /* Offset 1 */
    uint8_t magic;              /* Offset 2: 0x80 */
    uint8_t op_type;            /* Offset 3: 'X', 'B', '<', or '>' */
    uint8_t data[0x10];         /* Offset 4: varies by operation */
    uint8_t re_sids[0x14];      /* Offset 0x14: RE SIDs */
    uint8_t sids[0x24];         /* Offset 0x28: SIDs */
    uint8_t proj_list[0x40];    /* Offset 0x4C: Project list */
    uint8_t proj_out[2];        /* Offset 0x8C: Project output */
    uint16_t extra_len;         /* Offset 0x8E: varies */
    uint16_t extra_len2;        /* Offset 0x90: varies */
    void *extra_ptr;            /* Offset 0x92: varies */
    uint32_t bulk_len;          /* Offset 0x96: bulk data length */
    void *bulk_ptr;             /* Offset 0x9A: bulk data pointer (for 'B') */
    uint8_t padding[0x10];      /* Offset 0x9E-0xAD */
    void *extra_ptr2;           /* Offset 0xAC: extra pointer for 'X' */
    uint8_t copy_area[0x22];    /* Offset 0xB0: copy area for 'X' */
} rem_file_rn_op_buf_t;

/*
 * Remote operation response structure
 */
typedef struct {
    uint32_t padding;
    status_$t status;           /* Status at offset 4 */
} rem_file_rn_op_resp_t;

void REM_FILE_$RN_DO_OP(void *addr_info, rem_file_rn_op_buf_t *op_buf,
                         int16_t base_len, uint16_t response_size,
                         rem_file_rn_op_resp_t *response, void *extra_out)
{
    uint8_t re_sids[40];
    uint8_t sids_out[16];
    uint16_t bulk_in_len;
    uint16_t bulk_out_len;
    void *bulk_in_ptr;
    void *bulk_out_ptr;
    int16_t request_len;
    uint16_t received_len;
    uint16_t packet_id;
    status_$t local_status;

    /* Get RE SIDs */
    ACL_$GET_RE_ALL_SIDS(re_sids, &op_buf->sids, sids_out, &op_buf->re_sids,
                         &response->status);
    if (response->status != status_$ok) {
        return;
    }

    /* Get project list */
    ACL_$GET_PROJ_LIST((void *)&op_buf->proj_list, (void *)&default_proj_uid,
                       &op_buf->proj_out, &response->status);
    if (response->status != status_$ok) {
        return;
    }

    /* Check if running in subsystem - set flag if so */
    if (ACL_$IN_SUBSYS() < 0) {
        op_buf->data[0x1D] |= 0x04;
    }

    /* Set magic byte */
    op_buf->magic = 0x80;
    request_len = base_len;

    /* Handle operation-specific bulk data configuration */
    if (op_buf->op_type == 'X') {
        /* Type X: check if data fits in request buffer */
        if ((int32_t)(base_len + op_buf->extra_len) > 0x122) {
            /* Too large - use separate bulk transfer */
            bulk_in_len = op_buf->extra_len;
            bulk_in_ptr = op_buf->extra_ptr;
        } else {
            /* Copy data into request buffer */
            request_len = base_len + op_buf->extra_len;
            OS_$DATA_COPY(op_buf->extra_ptr, &op_buf->copy_area, op_buf->extra_len);
            bulk_in_len = 0;
            bulk_in_ptr = op_buf;
        }
    } else if (op_buf->op_type == '<') {
        /* Type <: check if data fits */
        if ((int32_t)base_len + (int32_t)op_buf->extra_len2 > 0x108) {
            bulk_in_len = op_buf->extra_len2;
            bulk_in_ptr = op_buf->extra_ptr;
        } else {
            request_len = base_len + op_buf->extra_len2;
            char *dst = (char *)op_buf + op_buf->extra_len + 0x96;
            OS_$DATA_COPY(op_buf->extra_ptr, dst, op_buf->extra_len2);
            bulk_in_len = 0;
            bulk_in_ptr = op_buf;
        }
    } else {
        bulk_in_len = 0;
        bulk_in_ptr = op_buf;
    }

    /* Configure output bulk data */
    if (op_buf->op_type == 'X') {
        bulk_out_len = 0x400;
        bulk_out_ptr = op_buf->extra_ptr2;
    } else if (op_buf->op_type == 'B') {
        bulk_out_ptr = op_buf->bulk_ptr;
        bulk_out_len = (op_buf->bulk_len < 0x400) ? (uint16_t)op_buf->bulk_len : 0x400;
    } else if (op_buf->op_type == '>') {
        bulk_out_len = op_buf->extra_len2;
        bulk_out_ptr = op_buf->extra_ptr;
    } else {
        bulk_out_len = 0;
        bulk_out_ptr = response;
    }

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, op_buf, request_len,
                           bulk_in_ptr, bulk_in_len,
                           response, response_size,
                           extra_out, bulk_out_ptr, bulk_out_len,
                           (int16_t *)&received_len, &packet_id,
                           &local_status);

    response->status = local_status;
}
