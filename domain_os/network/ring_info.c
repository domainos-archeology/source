/*
 * NETWORK_$RING_INFO - Get token ring network information
 *
 * Queries the network partner for token ring status information.
 * Sends command 0x0E to the specified network handle and returns
 * 122 bytes of ring information on success.
 *
 * The function:
 * 1. Sets up a command buffer with opcode 0x0E (RING_INFO)
 * 2. Calls the internal network_$do_request helper
 * 3. On success (status == 0), copies 122 bytes of ring info to output
 *
 * Original address: 0x00E1039A
 */

#include "network/network_internal.h"

/*
 * Ring info response size: 30 longs + 1 word = 122 bytes
 */
#define RING_INFO_SIZE          122
#define RING_INFO_LONGS         30      /* Number of 32-bit words to copy */

/*
 * Response buffer layout:
 *   +0x00: 6-byte header
 *   +0x06: ring info data (122 bytes)
 */
#define RING_INFO_RESP_OFFSET   6

/*
 * NETWORK_$RING_INFO - Get token ring network information
 *
 * @param net_handle     Network handle/connection to query
 * @param ring_info      Output: ring information buffer (122 bytes)
 * @param status_ret     Output: status code
 */
void NETWORK_$RING_INFO(void *net_handle, ring_info_t *ring_info,
                        status_$t *status_ret)
{
    uint16_t cmd_buf[72];           /* Command buffer (0xC0 bytes / 2) */
    uint8_t resp_buf[256];          /* Response buffer (0x100 bytes) */
    uint8_t resp_info[6];           /* Response info */
    status_$t local_status;
    int16_t i;
    uint32_t *src;
    uint32_t *dst;

    /*
     * Set up the ring info command.
     * Command format: 2-byte opcode (0x000E)
     */
    cmd_buf[0] = NETWORK_CMD_RING_INFO;

    /*
     * Send the command and wait for response.
     * Parameters 4-6 are reserved/unused (pass 0).
     */
    network_$do_request(net_handle, cmd_buf, 2, 0, 0, 0,
                        resp_buf, resp_info, &local_status);

    /* Return the status */
    *status_ret = local_status;

    /*
     * On success, copy the ring info from the response buffer.
     * The ring info starts at offset 6 in the response and is 122 bytes:
     *   - 30 longs (120 bytes)
     *   - 1 word (2 bytes)
     */
    if (local_status == status_$ok) {
        src = (uint32_t *)(resp_buf + RING_INFO_RESP_OFFSET);
        dst = (uint32_t *)ring_info;

        /* Copy 30 longs (120 bytes) */
        for (i = RING_INFO_LONGS; i >= 0; i--) {
            *dst++ = *src++;
        }

        /* Copy final word (2 bytes) */
        *(uint16_t *)dst = *(uint16_t *)src;
    }
}
