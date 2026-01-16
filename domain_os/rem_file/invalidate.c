/*
 * REM_FILE_$INVALIDATE - Invalidate pages in a remote file
 *
 * Sends an invalidate request to a remote file server to mark
 * specified pages as invalid, forcing them to be re-fetched
 * from the server on next access.
 *
 * Original address: 0x00E623D8
 * Size: 128 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Invalidate request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x20 = invalidate */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint32_t start;         /* Starting page offset */
    uint32_t count;         /* Number of pages to invalidate */
    uint8_t flags;          /* Invalidation flags */
} rem_file_invalidate_req_t;

void REM_FILE_$INVALIDATE(uid_t *vol_uid, uid_t *uid, uint32_t start,
                          uint32_t count, int16_t flags, status_$t *status)
{
    rem_file_invalidate_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x20;  /* INVALIDATE opcode */
    request.file_uid = *uid;
    request.start = start;
    request.count = count;
    request.flags = (uint8_t)flags;

    /* Send request */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, 0x16,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
