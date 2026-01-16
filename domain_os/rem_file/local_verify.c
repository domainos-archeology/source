/*
 * REM_FILE_$LOCAL_VERIFY - Verify lock on remote file server
 *
 * Sends a verification request to a remote file server to verify
 * that a lock is still valid.
 *
 * Original address: 0x00E61E20
 * Size: 122 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Local verify request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x1A = local verify */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint8_t lock_info[34];  /* Lock info (8 uint32s + 1 uint16) */
} rem_file_local_verify_req_t;

void REM_FILE_$LOCAL_VERIFY(void *addr_info, void *lock_block, status_$t *status)
{
    rem_file_local_verify_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *src = (uint32_t *)lock_block;
    uint32_t *dst;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x1A;  /* LOCAL_VERIFY opcode */

    /* Copy file UID from lock block */
    request.file_uid.high = src[0];
    request.file_uid.low = src[1];

    /* Copy 8 uint32s + 1 uint16 of lock info */
    dst = (uint32_t *)request.lock_info;
    for (i = 0; i < 8; i++) {
        dst[i] = src[i];
    }
    ((uint16_t *)dst)[16] = ((uint16_t *)src)[16];

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x2E,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
