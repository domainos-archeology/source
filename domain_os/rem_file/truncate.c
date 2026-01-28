/*
 * REM_FILE_$TRUNCATE - Truncate a remote file
 *
 * Sends a truncate request to a remote file server to change a file's size.
 *
 * Original address: 0x00E61976
 * Size: 188 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Truncate request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x08 = truncate */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint8_t flags;          /* Truncation flags */
    uint32_t new_size;      /* New file size */
    uint16_t reserved;      /* Reserved (value 3) */
    int8_t admin_flag;      /* Process admin flag */
} rem_file_truncate_req_t;

/*
 * Truncate response structure
 */
typedef struct {
    uint8_t padding[0xBC - 0xB8];
    uint32_t clock_high;    /* Clock high word */
    uint16_t clock_low;     /* Clock low word */
} rem_file_truncate_resp_t;

void REM_FILE_$TRUNCATE(uid_t *vol_uid, uid_t *uid, uint32_t new_size,
                        uint16_t flags, uint8_t *result, status_$t *status)
{
    rem_file_truncate_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    clock_t *clock_out = (clock_t *)result;  /* Result is actually a clock_t */

    /* Build request */
    request.magic = 0x80;
    request.opcode = REM_FILE_OP_TRUNCATE;
    request.file_uid = *uid;
    request.flags = (uint8_t)flags;
    request.new_size = new_size;
    request.reserved = 3;
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? -1 : 0;

    /* Send request */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, 0x16,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Extract clock from response if successful */
    if (received_len == 0x10) {
        /* Response contains clock value */
        rem_file_truncate_resp_t *resp = (rem_file_truncate_resp_t *)response;
        clock_out->high = resp->clock_high;
        clock_out->low = resp->clock_low;
    } else {
        /* Get current time instead */
        TIME_$CLOCK(clock_out);
    }
}
