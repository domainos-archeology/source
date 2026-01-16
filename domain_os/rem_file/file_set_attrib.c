/*
 * REM_FILE_$FILE_SET_ATTRIB - Set file attributes on remote file server
 *
 * Sets the attributes for a file on a remote server.
 * Returns the modification time if successful.
 *
 * Original address: 0x00E62C22
 * Size: 200 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * File set attribute request structure
 */
typedef struct {
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x82 = File set attrib */
    uid_t file_uid;             /* File UID (8 bytes) */
    uint16_t flags2;            /* Flags 2 */
    uint16_t flags;             /* Flags */
    uint32_t attrib_data1[13];  /* Attribute data block 1 (52 bytes) */
    uint32_t attrib_data2[25];  /* Attribute data block 2 (100 bytes) */
    uint16_t extra_flags;       /* Extra flags */
} rem_file_file_set_attrib_req_t;

/*
 * File set attribute response structure
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0x84];
    uint32_t mtime_high;        /* Modification time high */
    uint16_t mtime_low;         /* Modification time low */
} rem_file_file_set_attrib_resp_t;

void REM_FILE_$FILE_SET_ATTRIB(void *addr_info, uid_t *file_uid,
                                void *attrib_data1, uint16_t flags,
                                void *attrib_data2, uint16_t extra_flags,
                                uint16_t flags2, clock_t *mtime_out,
                                status_$t *status)
{
    rem_file_file_set_attrib_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint32_t *data1 = (uint32_t *)attrib_data1;
    uint32_t *data2 = (uint32_t *)attrib_data2;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x82;  /* FILE_SET_ATTRIB opcode */
    request.file_uid = *file_uid;

    /* Copy attribute data block 1 (13 uint32s) */
    for (i = 0; i < 13; i++) {
        request.attrib_data1[i] = data1[i];
    }

    /* Copy attribute data block 2 (25 uint32s) */
    for (i = 0; i < 25; i++) {
        request.attrib_data2[i] = data2[i];
    }

    request.flags = flags;
    request.flags2 = flags2;
    request.extra_flags = extra_flags;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0xAC,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Extract modification time from response or get current time */
    if (received_len == REM_FILE_RESPONSE_BUF_SIZE) {
        rem_file_file_set_attrib_resp_t *resp = (rem_file_file_set_attrib_resp_t *)response;
        mtime_out->high = resp->mtime_high;
        mtime_out->low = resp->mtime_low;
    } else {
        TIME_$CLOCK(mtime_out);
    }
}
