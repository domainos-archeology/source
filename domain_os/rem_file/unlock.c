/*
 * REM_FILE_$UNLOCK - Unlock a remote file
 *
 * Sends an unlock request to a remote file server.
 *
 * Original address: 0x00E61D1C
 * Size: 260 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * External function declarations
 */
extern void AST_$SET_DTS(uint16_t flags, void *uid, void *clock, void *data, void *out);

/*
 * Unlock request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x0C = unlock */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint32_t lock_key;      /* Lock key */
    uint32_t remote_node;   /* Remote node that holds lock */
    uint16_t unlock_mode;   /* Unlock mode */
    uint16_t flags;         /* Flags (value 3) */
    int8_t admin_flag;      /* Process admin flag */
    uint16_t wait_flag;     /* Wait flag */
    uint8_t release_flag;   /* Release flag */
} rem_file_unlock_req_t;

/*
 * Unlock response structure
 */
typedef struct {
    uint32_t clock_val;     /* Clock value */
    uint8_t padding[2];
    uint8_t result;         /* Result byte */
    int8_t dts_flag;        /* DTS update flag */
    uint8_t dts_data[176];  /* DTS data */
} rem_file_unlock_resp_t;

uint8_t REM_FILE_$UNLOCK(void *location_block, uint16_t unlock_mode,
                         uint32_t lock_key, uint16_t wait_flag,
                         uint32_t remote_node, int16_t release_flag,
                         status_$t *status)
{
    rem_file_unlock_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    uint32_t *loc = (uint32_t *)location_block;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x0C;  /* UNLOCK opcode */

    /* Copy file UID from location block offset +8 */
    request.file_uid.high = loc[2];
    request.file_uid.low = loc[3];

    request.lock_key = lock_key;
    request.remote_node = remote_node;
    request.wait_flag = wait_flag;
    request.unlock_mode = unlock_mode;
    request.flags = 3;
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? -1 : 0;
    request.release_flag = (uint8_t)release_flag;

    /* Send request (address info at location_block + 0x10) */
    REM_FILE_$SEND_REQUEST((uint8_t *)location_block + 0x10, &request, 0x22,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Process response */
    if ((int16_t)received_len <= 8) {
        return 0;
    }

    rem_file_unlock_resp_t *resp = (rem_file_unlock_resp_t *)&response[REM_FILE_RESPONSE_BUF_SIZE - sizeof(rem_file_unlock_resp_t)];
    uint16_t dts_flags = 0;

    /* Check if clock value is non-zero */
    if (resp->clock_val != 0) {
        dts_flags = 2;
    }

    /* Check for DTS update needed */
    if (release_flag < 0 && resp->dts_flag < 0 && (int16_t)received_len > 0x15) {
        dts_flags |= 8;
    }

    /* Update AST DTS if needed */
    if (dts_flags != 0) {
        AST_$SET_DTS(dts_flags, (uint8_t *)location_block + 8,
                     &resp->clock_val, resp->dts_data, &response[4]);
    }

    return resp->result;
}
