/*
 * XNS IDP Miscellaneous Functions
 *
 * Implementation of remaining IDP functions:
 *   - XNS_IDP_$GET_STATS
 *   - XNS_IDP_$GET_PORT_INFO
 *   - XNS_IDP_$REGISTER_ADDR
 *   - XNS_IDP_$PROC2_CLEANUP
 *
 * Original addresses:
 *   XNS_IDP_$GET_STATS:       0x00E18FD6
 *   XNS_IDP_$GET_PORT_INFO:   0x00E18FB8
 *   XNS_IDP_$REGISTER_ADDR:   0x00E19002
 *   XNS_IDP_$PROC2_CLEANUP:   0x00E18F0E
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$GET_STATS - Get IDP statistics
 *
 * Returns the global IDP statistics counters:
 *   - packets_sent: Total packets sent
 *   - packets_received: Total packets received
 *   - packets_dropped: Total packets dropped/errored
 *
 * @param stats         Output: statistics structure
 * @param status_ret    Output: status code (always status_$ok)
 *
 * Original address: 0x00E18FD6
 */
void XNS_IDP_$GET_STATS(xns_$idp_stats_t *stats, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;

    stats->packets_sent = *(uint32_t *)(base + XNS_OFF_PACKETS_SENT);
    stats->packets_received = *(uint32_t *)(base + XNS_OFF_PACKETS_RECV);
    stats->packets_dropped = *(uint32_t *)(base + XNS_OFF_PACKETS_DROP);

    *status_ret = status_$ok;
}

/*
 * XNS_IDP_$GET_PORT_INFO - Get port information
 *
 * This function is not implemented and always returns
 * status_$mac_port_op_not_implemented.
 *
 * @param channel       Channel number (unused)
 * @param port_info     Output: port information (unused)
 * @param status_ret    Output: always status_$mac_port_op_not_implemented
 *
 * Original address: 0x00E18FB8
 */
void XNS_IDP_$GET_PORT_INFO(void *channel, void *port_info, status_$t *status_ret)
{
    (void)channel;
    (void)port_info;

    *status_ret = status_$mac_port_op_not_implemented;
}

/*
 * XNS_IDP_$REGISTER_ADDR - Register an additional network address
 *
 * Registers an additional XNS network address for this node. This allows
 * the node to respond to packets addressed to multiple addresses.
 * Up to 4 addresses can be registered.
 *
 * If the port already has a registered address, the address is updated.
 * Otherwise, a new entry is added.
 *
 * @param addr          Address to register (6 bytes: network hi, mid, lo)
 * @param port          Pointer to port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E19002
 */
void XNS_IDP_$REGISTER_ADDR(uint16_t *addr, int16_t *port, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    int16_t reg_count = XNS_REG_COUNT();
    int16_t i;
    int16_t port_num = *port;

    *status_ret = status_$ok;

    /* Check if port already has a registered address */
    if (reg_count >= 0) {
        for (i = 0; i <= reg_count; i++) {
            int16_t existing_port = *(int16_t *)(base + 0x10 + i * 2);
            if (existing_port == port_num) {
                /* Update existing entry */
                int offset = i * 6;
                *(uint16_t *)(base + 0x20 + offset) = addr[0];
                *(uint16_t *)(base + 0x22 + offset) = addr[1];
                *(uint16_t *)(base + 0x24 + offset) = addr[2];
                return;
            }
        }
    }

    /* Add new entry */
    i = reg_count + 1;  /* Actually i should be count from loop, this is the count check */
    if (i >= XNS_MAX_ADDRS) {
        *status_ret = status_$xns_too_many_addrs;
        return;
    }

    /* Add at current count position */
    {
        int16_t count = XNS_REG_COUNT();
        int offset = count * 6;
        *(uint16_t *)(base + 0x26 + offset) = addr[0];
        *(uint16_t *)(base + 0x28 + offset) = addr[1];
        *(uint16_t *)(base + 0x2A + offset) = addr[2];
        *(int16_t *)(base + 0x12 + count * 2) = port_num;
        XNS_REG_COUNT() += 1;
    }
}

/*
 * XNS_IDP_$PROC2_CLEANUP - Clean up channels for a terminating process
 *
 * Called when a process (address space) terminates. This function finds
 * all IDP channels owned by the terminating AS and closes them.
 *
 * @param as_id         Address space ID of terminating process
 *
 * Original address: 0x00E18F0E
 */
void XNS_IDP_$PROC2_CLEANUP(uint16_t as_id)
{
    uint8_t *base = XNS_IDP_BASE;
    int16_t chan;
    int16_t port;
    status_$t local_status;

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START((ml_$exclusion_t *)(base + XNS_OFF_LOCK));

    /* Scan all channels */
    for (chan = 0; chan < XNS_MAX_CHANNELS; chan++) {
        uint8_t *chan_base = base + chan * XNS_CHANNEL_SIZE;

        /* Check if channel is active */
        if (*(int16_t *)(chan_base + XNS_CHAN_OFF_STATE) >= 0) {
            continue;  /* Not active */
        }

        /* Check if channel is owned by this AS */
        uint16_t chan_as_id = (*(uint16_t *)(chan_base + XNS_CHAN_OFF_FLAGS) &
                               XNS_CHAN_FLAG_AS_ID_MASK) >> XNS_CHAN_FLAG_AS_ID_SHIFT;
        if (chan_as_id != as_id) {
            continue;  /* Not owned by this AS */
        }

        /* Close user socket if allocated */
        {
            uint16_t user_socket = *(uint16_t *)(chan_base + XNS_CHAN_OFF_USER_SOCKET);
            if (user_socket != XNS_NO_SOCKET) {
                SOCK_$CLOSE(user_socket);
            }
        }

        /* Delete all port bindings */
        for (port = 0; port < XNS_MAX_PORTS; port++) {
            if (*(int8_t *)(chan_base + XNS_CHAN_OFF_PORT_ACTIVE + port) < 0) {
                xns_$delete_port(chan, port, &local_status);
            }
        }

        /* Clear channel state */
        *(uint8_t *)(chan_base + XNS_CHAN_OFF_STATE) &= 0x7F;         /* Clear active flag */
        *(uint8_t *)(chan_base + XNS_CHAN_OFF_FLAGS) &= 0x07;         /* Clear flags */
        *(uint16_t *)(chan_base + XNS_CHAN_OFF_USER_SOCKET) = XNS_NO_SOCKET;
        *(uint16_t *)(chan_base + XNS_CHAN_OFF_XNS_SOCKET) = 0;

        /* Decrement open channel count */
        XNS_OPEN_COUNT() -= 1;
    }

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
}
