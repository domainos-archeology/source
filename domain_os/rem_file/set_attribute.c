/*
 * REM_FILE_$SET_ATTRIBUTE - Set attribute on a remote file
 *
 * Sends an attribute change request to a remote file server.
 * Used for operations like setting timestamps, flags, etc.
 *
 * Original address: 0x00E61A32
 * Size: 124 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Set attribute request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x04 = set attribute */
    uid_t file_uid;         /* File UID (8 bytes) */
    uint16_t attr_id;       /* Attribute identifier */
    uint8_t attr_data[52];  /* Attribute-specific data (13 uint32s) */
} rem_file_set_attr_req_t;

void REM_FILE_$SET_ATTRIBUTE(void *vol_uid, uid_t *file_uid,
                             uint16_t attr_id, void *attr_data,
                             status_$t *status)
{
    rem_file_set_attr_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *src = (uint32_t *)attr_data;
    uint32_t *dst = (uint32_t *)request.attr_data;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x04;  /* SET_ATTRIBUTE opcode */
    request.file_uid = *file_uid;
    request.attr_id = attr_id;

    /* Copy 13 uint32s of attribute data */
    for (i = 0; i < 13; i++) {
        dst[i] = src[i];
    }

    /* Send request */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, 0x42,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
