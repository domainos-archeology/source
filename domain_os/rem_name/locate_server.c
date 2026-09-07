/*
 * rem_name/locate_server.c - REM_NAME_$LOCATE_SERVER (0x00E4A722)
 *
 * Part of the REM_NAME module (SAU2 map, I 0xE4A408 size 0xB20).
 * Split out of the single name/rem_name.c (bead source-ev4k); the body
 * below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME_$LOCATE_SERVER - Locate a naming server
 *
 * First tries the local node, then broadcasts to find a server.
 * Returns the node/network ID of the located server.
 *
 * Parameters:
 *   node_ret   - Output: server node ID
 *   net_ret    - Output: server network ID (0 if local)
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a722
 * Original size: 222 bytes
 */
void REM_NAME_$LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uint8_t  reserved[8];
        uint16_t flags;
    } request;

    uint8_t response[0x16a];
    int16_t resp_len;
    uint32_t located_node;

    request.opcode = REM_NAME_OP_LOCATE_SERVER;
    request.flags = 1;

    /* First check if server is local */
    if (REM_NAME_SERVER_LOCAL()) {
        /* Try local node first */
        if (rem_name_$send_request(0, NODE_$ME, &request, 0x32, 0, 0x1e,
                                    response, 0x16a, &resp_len, status_ret)) {
            if (resp_len >= 0x28) {
                goto found_server;
            }
        }
    }

    /* Broadcast to find a server (node 0xFFFFFF = broadcast) */
    if (!rem_name_$send_request(0, 0xFFFFFF, &request, 0x32, 0x80, 0x1e,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    if (resp_len < 0x28) {
        *status_ret = status_$naming_helper_sent_packets_with_errors;
        rem_name_$data.last_status = *status_ret;
        return;
    }

found_server:
    /* Extract server node from response (20-bit node ID) */
    located_node = *(uint32_t *)(response + 0x18) & 0xFFFFF;

    *net_ret = 0;
    *node_ret = located_node;
    rem_name_$data.curr_node = located_node;
    rem_name_$data.curr_net = 0;
    rem_name_$data.last_status = *status_ret;
}
