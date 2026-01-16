/*
 * REM_FILE_$LOCAL_READ_LOCK - Read lock information from remote server
 *
 * Reads lock entry information from a remote file server.
 *
 * Original address: 0x00E61E9A
 * Size: 164 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Local read lock request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x16 = local read lock */
    uid_t file_uid;         /* File UID (8 bytes) */
} rem_file_local_read_lock_req_t;

/*
 * Local read lock response structure
 * Contains lock entry data (8 uint32s + 1 uint16 = 34 bytes)
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xB8];
    uint32_t lock_data[8];  /* Lock entry data */
    uint16_t lock_extra;    /* Extra lock info */
} rem_file_local_read_lock_resp_t;

void REM_FILE_$LOCAL_READ_LOCK(void *addr_info, uid_t *file_uid,
                               void *lock_entry_out, status_$t *status)
{
    rem_file_local_read_lock_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x16;  /* LOCAL_READ_LOCK opcode */
    request.file_uid = *file_uid;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x0C,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Copy lock data from response if successful */
    if (*status == status_$ok) {
        rem_file_local_read_lock_resp_t *resp = (rem_file_local_read_lock_resp_t *)response;
        uint32_t *out = (uint32_t *)lock_entry_out;

        /* Copy 8 uint32s + 1 uint16 */
        for (i = 0; i < 8; i++) {
            out[i] = resp->lock_data[i];
        }
        ((uint16_t *)out)[16] = resp->lock_extra;
    }

    /* Handle different response lengths - clear trailing fields */
    if (received_len == 0x22) {
        /* Clear fields at offset 0x1A and 0x1E */
        ((uint32_t *)lock_entry_out)[0x1A / 4] = 0;
        ((uint32_t *)lock_entry_out)[0x1E / 4] = 0;
    }
    if (received_len == 0x26) {
        /* Clear field at offset 0x1E only */
        ((uint32_t *)lock_entry_out)[0x1E / 4] = 0;
    }
}
