/*
 * REM_FILE_$LOCK - Lock a remote file
 *
 * Sends a lock request to a remote file server. Supports both
 * simple locks (opcode 0x0A) and extended locks (opcode 0x84).
 *
 * Original address: 0x00E61AAE
 * Size: 452 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Simple lock request structure (opcode 0x0A)
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x0A = simple lock */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint32_t lock_key;      /* Lock key */
    uint32_t src_node;      /* Source node ID */
    uint16_t lock_mode;     /* Lock mode */
    uint16_t lock_type;     /* Lock type */
    uint16_t flags;         /* Flags (value 3) */
    int8_t admin_flag;      /* Process admin flag */
    uint16_t reserved;      /* Reserved (value 1) */
} rem_file_simple_lock_req_t;

/*
 * Extended lock request structure (opcode 0x84)
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x84 = extended lock */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint8_t location_info[32]; /* Location info (8 uint32s) */
    uint32_t lock_key;      /* Lock key */
    uint32_t src_node;      /* Source node ID */
    uint16_t lock_mode;     /* Lock mode */
    uint16_t lock_type;     /* Lock type */
    uint16_t flags;         /* Flags */
    uint16_t reserved;      /* Reserved (value 1) */
    uint8_t exsid_data[100]; /* Extended SID data */
    uint16_t wait_flag;     /* Wait flag */
} rem_file_ext_lock_req_t;

/*
 * Lock response structure
 */
typedef struct {
    uint8_t padding[0xBC - 0xB8];
    uint32_t lock_result;   /* Lock result/handle */
    uint16_t lock_info[72]; /* Lock info */
} rem_file_lock_resp_t;

void REM_FILE_$LOCK(void *location_block, uint16_t lock_mode, uint16_t lock_type,
                    uint16_t flags, uint16_t wait_flag, int8_t extended,
                    uint32_t lock_key, uint16_t *packet_id_out, uint16_t *status_word,
                    void *lock_result, status_$t *status)
{
    uint8_t request_buf[0x174];
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t request_len;
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *loc_block = (uint32_t *)location_block;
    uint32_t *result = (uint32_t *)lock_result;

    if (extended < 0) {
        /* Extended lock request */
        rem_file_ext_lock_req_t *req = (rem_file_ext_lock_req_t *)request_buf;
        uint32_t *dst;
        int8_t in_subsys;

        req->magic = 0x80;
        req->opcode = 0x84;

        /* Copy file UID from location block offset +8 */
        req->file_uid.high = loc_block[2];
        req->file_uid.low = loc_block[3];

        /* Copy location info (8 uint32s) */
        dst = (uint32_t *)req->location_info;
        for (i = 0; i < 8; i++) {
            dst[i] = loc_block[i];
        }

        req->lock_key = lock_key;
        req->src_node = NODE_$ME;
        req->lock_mode = lock_mode;
        req->lock_type = lock_type;
        req->flags = flags;

        /* Check if in subsystem */
        in_subsys = ACL_$IN_SUBSYS();
        if (in_subsys < 0) {
            req->flags |= 0x100;
        }

        req->wait_flag = wait_flag;
        req->reserved = 1;

        /* Get extended SID */
        ACL_$GET_EXSID(req->exsid_data, status);
        if (*status != status_$ok) {
            return;
        }

        request_len = 0xA2;
    } else {
        /* Simple lock request */
        rem_file_simple_lock_req_t *req = (rem_file_simple_lock_req_t *)request_buf;

        req->magic = 0x80;
        req->opcode = 0x0A;

        /* Copy file UID from location block offset +8 */
        req->file_uid.high = loc_block[2];
        req->file_uid.low = loc_block[3];

        req->lock_key = lock_key;
        req->src_node = NODE_$ME;
        req->lock_mode = lock_mode;
        req->lock_type = lock_type;
        req->flags = 3;
        req->admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? -1 : 0;
        req->reserved = 1;

        request_len = 0x22;
    }

    /* Send request (address info at location_block + 0x10) */
    REM_FILE_$SEND_REQUEST((uint8_t *)location_block + 0x10, request_buf, request_len,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    *status_word = packet_id;

    if (*status == status_$ok) {
        rem_file_lock_resp_t *resp = (rem_file_lock_resp_t *)response;

        if (extended < 0) {
            /* Copy 36 uint32s (0x24 words) from response */
            uint16_t *src = resp->lock_info;
            for (i = 0; i < 36; i++) {
                result[i] = *(uint32_t *)src;
                src += 2;
            }

            /* Update location block with response data */
            uint32_t saved_high = loc_block[4];
            uint32_t saved_low = loc_block[5];
            uint32_t *resp_loc = (uint32_t *)&response[0xBC - 0x24];
            for (i = 0; i < 8; i++) {
                loc_block[i] = resp_loc[i];
            }
            /* Preserve part and set flag */
            loc_block[4] = saved_high;
            loc_block[5] = saved_low;
            ((uint8_t *)location_block)[0x1d] |= 0x80;

            /* Copy packet ID */
            *packet_id_out = *(uint16_t *)&response[REM_FILE_RESPONSE_BUF_SIZE - 4];
        } else {
            /* Copy lock result */
            result[0xB] = resp->lock_result;

            /* Process lock info based on response length */
            if (received_len == 0x12) {
                ((uint16_t *)result)[0x18] = 0;
            } else if (received_len == 0x0E) {
                ((uint16_t *)result)[0x18] = (resp->lock_info[0] & 0xF8) << 8;
            } else {
                ((uint16_t *)result)[0x18] = resp->lock_info[0];
            }
        }
    }
}
