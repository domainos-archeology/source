/*
 * rem_name/send_request.c - rem_name_$send_request (0x00E4A4C8)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/* Callback data for PKT_$SAR_INTERNET - at 0x00e4a584 */
/*
 * 0x00E4A584: the empty request-data block PKT_$SAR_INTERNET is handed with
 * a length of 0 (`pea (0x6a,PC)` at 0x00E4A518; PC = 0x00E4A51A).
 */
static const uint8_t pkt_callback_data[] = { 0, 0, 0, 0 };

/*
 * rem_name_$send_request - Send a remote naming request (internal)
 *
 * Core RPC mechanism for remote naming operations. Copies configuration
 * data, adds flags, and calls PKT_$SAR_INTERNET to send the request.
 *
 * Parameters:
 *   net        - Network ID (0 for local)
 *   node       - Node ID to contact
 *   request    - Request data buffer
 *   req_size   - Size of request data
 *   flags      - Additional flags to OR into config[0]
 *   opcode     - Operation code for validation
 *   response   - Response buffer
 *   resp_size  - Size of response buffer
 *   resp_len_ret - Output: actual response length
 *   status_ret - Output: status code
 *
 * Returns:
 *   0xFF (true) on success with valid response, 0 (false) otherwise
 *
 * Original address: 0x00e4a4c8
 * Original size: 188 bytes
 */
boolean rem_name_$send_request(uint32_t net, uint32_t node, void *request,
                                       int16_t req_size, int16_t flags, int16_t opcode,
                                       void *response, int16_t resp_size,
                                       int16_t *resp_len_ret, status_$t *status_ret)
{
    /*
     * A6-0x48: 0x00E4A4F0 copies seven longwords and then one more word,
     * i.e. exactly 15 words, from the module block at A5.
     */
    uint16_t config[15];
    uint8_t out_buf[40];        /* A6-0x28 */
    uint8_t out1[4];            /* A6-0x4C */
    uint16_t out2;              /* A6-0x52: response data length out */
    status_$t internal_status;  /* A6-0x50 */
    int i;

    /* Copy configuration data from global structure */
    for (i = 0; i < 15; i++) {
        config[i] = rem_name_$data.config[i];
    }

    /* OR in additional flags */
    config[0] |= (uint16_t)flags;

    /* Send the packet */
    PKT_$SAR_INTERNET(net, node, 10, config, rem_name_$data.service_delay,
                      request, req_size, (void *)pkt_callback_data, 0,
                      out_buf, response, resp_size, resp_len_ret,
                      out1, 0, &out2, &internal_status);

    if (internal_status != status_$ok) {
        *status_ret = internal_status;
        rem_name_$data.last_status = internal_status;
        return false;
    }

    /* Validate response */
    if (*resp_len_ret < 0x12) {
        /* Response too short */
        *status_ret = *(status_$t *)((uint8_t *)response + 0x0e);
        rem_name_$data.last_status = *status_ret;
        return false;
    }

    /* Check opcode matches */
    if ((int16_t)opcode != *(int16_t *)((uint8_t *)response + 0x02)) {
        *status_ret = *(status_$t *)((uint8_t *)response + 0x0e);
        rem_name_$data.last_status = *status_ret;
        return false;
    }

    /* Check for error in response */
    if (*(status_$t *)((uint8_t *)response + 0x0e) != status_$ok) {
        *status_ret = *(status_$t *)((uint8_t *)response + 0x0e);
        rem_name_$data.last_status = *status_ret;
        return false;
    }

    *status_ret = status_$ok;
    return true;
}
