/*
 * REM_FILE_$RESERVE - Reserve space in a remote file
 *
 * Sends a reserve request to a remote file server to pre-allocate
 * disk space for a file.
 *
 * Original address: 0x00E62458
 * Size: 148 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Reserve request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x7C = reserve */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint32_t start;         /* Starting byte offset */
    uint32_t count;         /* Number of bytes to reserve */
} rem_file_reserve_req_t;

void REM_FILE_$RESERVE(uid_t *vol_uid, uid_t *uid, uint32_t start,
                       uint32_t count, status_$t *status)
{
    rem_file_reserve_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x7C;  /* RESERVE opcode */
    request.file_uid = *uid;
    request.start = start;
    request.count = count;

    /* Send request */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, 0x16,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Special case: if status is 0xF0003 (bad reply) and response
     * byte at offset 3 is 3, treat as success */
    if (*status == 0x000F0003 && response[3] == 0x03) {
        *status = status_$ok;
    }
}
