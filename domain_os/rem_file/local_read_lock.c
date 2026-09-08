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
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x16 = local read lock */
    uid_t file_uid;         /* File UID (8 bytes) */
} rem_file_local_read_lock_req_t;

/*
 * Local read lock response structure
 * Contains lock entry data (8 uint32s + 1 uint16 = 34 bytes)
 */
typedef struct {
    uint8_t head[0x08];     /* 0x00; the payload starts at response+0x08
                             * ("lea (-0xb8,A6),A0" at 0x00E61F0A, buffer at
                             * A6-0xC0) */
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
    request.opcode = REM_FILE_OP_LOCAL_READ_LOCK;  /* 0x16, 0x00E61EB6 */
    request.file_uid = *file_uid;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x0C,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* 0x00E61F06-0x00E61F1A: 8 longwords plus a word - 34 bytes - from
     * response+0x08, but only when the transport succeeded. */
    if (*status == status_$ok) {
        rem_file_local_read_lock_resp_t *resp = (rem_file_local_read_lock_resp_t *)response;
        uint32_t *out = (uint32_t *)lock_entry_out;

        for (i = 0; i < 8; i++) {
            out[i] = resp->lock_data[i];
        }
        ((uint16_t *)out)[16] = resp->lock_extra;
    }

    /* 0x00E61F1C-0x00E61F32.  The two clears are UNALIGNED longwords at BYTE
     * offsets 0x1A and 0x1E of the caller's record ("clr.l (0x1a,A3)" /
     * "clr.l (0x1e,A3)"), and they run whatever the status was. */
    {
        uint8_t *out_b = (uint8_t *)lock_entry_out;
        if (received_len == 0x22) {
            out_b[0x1A] = 0; out_b[0x1B] = 0; out_b[0x1C] = 0; out_b[0x1D] = 0;
            out_b[0x1E] = 0; out_b[0x1F] = 0; out_b[0x20] = 0; out_b[0x21] = 0;
        }
        if (received_len == 0x26) {
            out_b[0x1E] = 0; out_b[0x1F] = 0; out_b[0x20] = 0; out_b[0x21] = 0;
        }
    }
}
