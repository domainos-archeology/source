/*
 * REM_FILE_$PURIFY - Purify (flush) a remote file to disk
 *
 * Sends a purify request to a remote node to flush modified pages
 * of a file to stable storage.
 *
 * Original address: 0x00E6225C
 * Size: 146 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Purify request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x0B = purify */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint16_t flags;         /* Purify flags */
    int16_t page_index;     /* Page index for partial purify */
    uint16_t reserved;      /* Reserved (value 3) */
    int8_t admin_flag;      /* Process admin flag */
} rem_file_purify_req_t;

void REM_FILE_$PURIFY(uid_t *vol_uid, uid_t *file_uid, uint16_t *flags,
                      int16_t page_index, status_$t *status)
{
    rem_file_purify_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = 0x80;
    request.opcode = REM_FILE_OP_PURIFY;
    request.file_uid = *file_uid;
    request.flags = *flags;
    request.page_index = page_index;
    request.reserved = 3;
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? -1 : 0;

    /* Send request */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, 0x16,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
