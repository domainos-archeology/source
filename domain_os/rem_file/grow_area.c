/*
 * REM_FILE_$GROW_AREA - Grow an area in a remote file
 *
 * Sends a request to extend the size of an area (extent) in a remote file.
 *
 * Original address: 0x00E62734
 * Size: 116 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Grow area request structure
 */
typedef struct {
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x8A = grow area */
    uint8_t padding[6];     /* Padding */
    uint32_t current_size;  /* Current area size */
    uint8_t padding2[4];    /* More padding */
    uint16_t area_handle;   /* Area handle */
    uint32_t new_size;      /* New area size */
} rem_file_grow_area_req_t;

void REM_FILE_$GROW_AREA(void *addr_info, uint16_t area_handle,
                         uint32_t current_size, uint32_t new_size,
                         status_$t *status)
{
    rem_file_grow_area_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x8A;  /* GROW_AREA opcode */
    request.area_handle = area_handle;
    request.current_size = current_size;
    request.new_size = new_size;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x1C,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
