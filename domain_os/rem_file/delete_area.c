/*
 * REM_FILE_$DELETE_AREA - Delete an area in a remote file
 *
 * Sends a request to delete an area (extent) in a remote file.
 *
 * Original address: 0x00E626CC
 * Size: 104 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Delete area request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x88 = delete area */
    uint8_t padding[10];    /* Padding */
    uint32_t area_offset;   /* Area offset */
    uint16_t area_handle;   /* Area handle */
} rem_file_delete_area_req_t;

void REM_FILE_$DELETE_AREA(void *addr_info, uint16_t area_handle,
                           uint32_t area_offset, status_$t *status)
{
    rem_file_delete_area_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x88;  /* DELETE_AREA opcode */
    request.area_handle = area_handle;
    request.area_offset = area_offset;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x1C,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
