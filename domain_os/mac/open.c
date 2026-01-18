/*
 * MAC_$OPEN - Open a MAC channel
 *
 * Opens a network channel on the specified port with the given packet
 * type filters.
 *
 * Original address: 0x00E0B8BE
 * Original size: 430 bytes
 */

#include "mac/mac_internal.h"

/*
 * The original function builds a complex stack frame for MAC_OS_$OPEN.
 * The frame includes:
 * - local_5c: status from MAC_OS_$OPEN
 * - local_58: channel number returned from MAC_OS_$OPEN
 * - local_c: pointer to MAC_$DEMUX callback
 * - local_8: number of packet types
 * - stack area for packet type min/max values
 *
 * The function also accesses:
 * - Port info table at 0xE2E0A0 (entry size 0x5C)
 * - Socket pointer array at 0xE28DB0
 * - Channel table at A5+0x7A8 (A5 = 0xE22990, so 0xE23138)
 * - Exclusion lock at A5+0x868 (0xE231F8)
 */

void MAC_$OPEN(int16_t *port_num, mac_$open_params_t *params, status_$t *status_ret)
{
    uint16_t sock_num;
    status_$t os_status;
    uint32_t os_handle;
    uint16_t channel_num;
    int16_t port;
    int16_t num_types;
    int16_t i;
    uint32_t *type_ptr;
    uint8_t *sock_ptr;
    uint32_t channel_offset;

    *status_ret = status_$ok;

    /* Validate port number (0-7) */
    port = *port_num;
    if (port < 0 || port > 7) {
        *status_ret = status_$mac_invalid_port;
        return;
    }

    /*
     * Check if port is open by examining port info table.
     * The port info at offset 0x2C contains the port type.
     * Types 0 and 1 (bits 0-1) indicate port is NOT available.
     * Original: if ((1 << (port_info[port].field_2c & 0x1f)) & 3) != 0) error
     */
#if defined(M68K)
    {
        uint16_t port_type = *(uint16_t *)(MAC_$PORT_INFO_BASE + port * MAC_PORT_INFO_SIZE + 0x2C);
        if ((1 << (port_type & 0x1F)) & 3) {
            *status_ret = status_$internet_network_port_not_open;
            return;
        }
    }
#else
    /* TODO: Non-M68K port info access */
#endif

    /* Validate packet type count (1-10) */
    num_types = params->num_packet_types;
    if (num_types < 1 || num_types > MAC_MAX_PACKET_TYPES) {
        *status_ret = status_$mac_invalid_packet_type_count;
        return;
    }

    /* Validate each packet type range (min <= max) */
    type_ptr = (uint32_t *)params->packet_types;
    for (i = num_types - 1; i >= 0; i--) {
        if (type_ptr[0] > type_ptr[1]) {
            *status_ret = status_$mac_invalid_packet_type;
            return;
        }
        type_ptr += 2;
    }

    /* Check that socket_count is non-zero */
    if (params->socket_count == 0) {
        *status_ret = status_$mac_no_socket_allocated;
        return;
    }

    /*
     * Allocate a user-mode socket.
     * SOCK_$ALLOCATE_USER returns negative on success.
     * Original passes socket_count for multiple parameters.
     */
    if (SOCK_$ALLOCATE_USER(&sock_num, params->socket_count,
                            params->socket_count, params->socket_count, 0x400) >= 0) {
        *status_ret = status_$mac_no_os_sockets_available;
        return;
    }

    /*
     * Clear bit 7 (0x80) of socket flags at offset 0x16.
     * This marks the socket for internal use until fully set up.
     * Original: bclr.b #0x7,(0x16,A3)
     */
#if defined(M68K)
    sock_ptr = (uint8_t *)(*(uint32_t *)(0xE28DB0 + sock_num * 4));
    sock_ptr[0x16] &= 0x7F;
#else
    /* TODO: Non-M68K socket access */
#endif

    /*
     * Build the frame and call MAC_OS_$OPEN.
     * The original code sets up:
     * - MAC_$DEMUX as callback at local_c
     * - num_packet_types at local_8
     * - Copies packet type min/max pairs to stack
     */
    MAC_OS_$OPEN(port_num, params, &os_status);
    *status_ret = os_status;

    if (os_status != status_$ok) {
        /* Failed - close the socket we allocated */
        SOCK_$CLOSE(sock_num);
        return;
    }

    /*
     * MAC_OS_$OPEN returns channel_num in the params structure.
     * Extract it for local use.
     */
    channel_num = params->channel_num;
    os_handle = params->os_handle;

    /* Set process cleanup handler for MAC (cleanup type 0x0D) */
    PROC2_$SET_CLEANUP(0x0D);

    /* Enter exclusion region to update channel table */
#if defined(M68K)
    ML_$EXCLUSION_START(&mac_$exclusion_lock);

    /*
     * Channel table entry calculation:
     * entry_offset = channel_num * 20 (0x14)
     * But original uses: channel_num * 4 * 4 + channel_num * 4 = channel_num * 20
     * Actually the assembly does: D0 = D2 << 2, D1 = D0 << 2, D0 = D0 + D1
     * So: D0 = chan*4, D1 = chan*16, D0 = chan*4 + chan*16 = chan*20
     */
    channel_offset = (uint32_t)channel_num * 20;

    /* Store socket number in channel table entry (offset 0x7A8 from base) */
    *(uint16_t *)(MAC_$DATA_BASE + 0x7A8 + channel_offset) = sock_num;

    /*
     * Set promiscuous flag from params->flags bit 7.
     * flags field is at offset 0x7B2 from base.
     * Original: andi.b #0xFE, or.b params.flags >> 7
     */
    {
        uint8_t *flags_ptr = (uint8_t *)(MAC_$DATA_BASE + 0x7B2 + channel_offset);
        uint8_t promisc = (params->flags >> 7) & 1;
        *flags_ptr = (*flags_ptr & 0xFE) | promisc;
    }

    ML_$EXCLUSION_STOP(&mac_$exclusion_lock);

    /*
     * Register the socket's EC1 with EC2 and return handle.
     * The socket structure pointer is at offset -4 from the indexed entry
     * in the socket array. The EC1 is within that structure.
     */
    sock_ptr = (uint8_t *)(*(uint32_t *)(0xE28DB4 + sock_num * 4 - 4));
    params->ec2_handle = EC2_$REGISTER_EC1((ec_$eventcount_t *)sock_ptr, status_ret);
#else
    ML_$EXCLUSION_START(&mac_$exclusion_lock);
    /* TODO: Non-M68K channel table access */
    ML_$EXCLUSION_STOP(&mac_$exclusion_lock);
#endif

    /* Store OS handle and channel number in params structure */
    *(uint32_t *)params = (uint32_t)(params->ec2_handle);  /* Already stored by EC2_$REGISTER_EC1 */
    ((uint32_t *)params)[1] = os_handle;
    ((uint16_t *)params)[4] = channel_num;
}
