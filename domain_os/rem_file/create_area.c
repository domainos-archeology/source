/*
 * REM_FILE_$CREATE_AREA - Create an area in a remote file
 *
 * Sends a request to create a new area (extent) in a remote file.
 * Returns the area handle and packet size.
 *
 * Original address: 0x00E62622
 * Size: 170 bytes
 */

#include "rem_file/rem_file_internal.h"

/*
 * Create area request structure
 */
typedef struct {
    uint16_t msg_type;      /* Set to 1 by SEND_REQUEST */
    uint8_t magic;          /* 0x80 */
    uint8_t opcode;         /* 0x86 = create area */
    uint8_t padding[6];     /* Padding */
    uint32_t area_type;     /* Area type */
    uint32_t area_size;     /* Area size */
    uint32_t area_offset;   /* Area offset */
} rem_file_create_area_req_t;

/*
 * Create area response structure
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 8];
    uint16_t area_handle;   /* Area handle */
    uint16_t pkt_size;      /* Packet size */
} rem_file_create_area_resp_t;

uint16_t REM_FILE_$CREATE_AREA(void *addr_info, uint32_t area_type,
                               uint32_t area_size, uint32_t area_offset,
                               uint8_t flags, uint16_t *pkt_size_out,
                               status_$t *status)
{
    rem_file_create_area_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    uint16_t pkt_size;

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x86;  /* CREATE_AREA opcode */
    request.area_type = area_type;
    request.area_offset = area_offset;
    request.area_size = area_size;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0x1C,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    /* Determine packet size from response or use default */
    if ((int16_t)received_len < 0x0B) {
        pkt_size = 0x400;  /* Default 1KB */
    } else {
        rem_file_create_area_resp_t *resp = (rem_file_create_area_resp_t *)response;
        pkt_size = resp->pkt_size;
    }

    /* Get adjusted packet size for this network */
    *pkt_size_out = NETWORK_$GET_PKT_SIZE(addr_info, pkt_size);

    /* Return area handle */
    rem_file_create_area_resp_t *resp = (rem_file_create_area_resp_t *)response;
    return resp->area_handle;
}
