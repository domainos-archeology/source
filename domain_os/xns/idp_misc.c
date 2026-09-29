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
 *
 * Module data through XNS_IDP_$DATA: Claude Opus 5.5 (source-iq58).
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
    /* 0x00E18FE6-0x00E18FEE: (A5), (0x4,A5), (0x8,A5) */
    stats->packets_sent = XNS_IDP_$DATA.packets_sent;
    stats->packets_received = XNS_IDP_$DATA.packets_received;
    stats->packets_dropped = XNS_IDP_$DATA.packets_dropped;

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
    int16_t reg_count = XNS_IDP_$DATA.registered_count;
    int16_t i;
    int16_t port_num = *port;

    *status_ret = status_$ok;

    /* Check if port already has a registered address */
    if (reg_count >= 0) {
        for (i = 0; i <= reg_count; i++) {
            /* 0x00E1902E: (0x10,A0), A0 = A5 + i*2 */
            int16_t existing_port = XNS_IDP_$DATA.addr_port[i];
            if (existing_port == port_num) {
                /* Update existing entry: (0x20/0x22/0x24,A5,i*6) */
                XNS_IDP_$DATA.addrs[i][0] = addr[0];
                XNS_IDP_$DATA.addrs[i][1] = addr[1];
                XNS_IDP_$DATA.addrs[i][2] = addr[2];
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

    /*
     * Append after the last entry: registered_count is the dbf count, so
     * the new element is count + 1 - (0x26/0x28/0x2a,A5,count*6) and
     * (0x12,A5,count*2), 0x00E19064-0x00E1909E.
     */
    {
        int16_t count = XNS_IDP_$DATA.registered_count;
        XNS_IDP_$DATA.addrs[count + 1][0] = addr[0];
        XNS_IDP_$DATA.addrs[count + 1][1] = addr[1];
        XNS_IDP_$DATA.addrs[count + 1][2] = addr[2];
        XNS_IDP_$DATA.addr_port[count + 1] = port_num;
        XNS_IDP_$DATA.registered_count += 1;    /* addq.w #1,(0x538,A5) */
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
    int16_t chan;
    int16_t port;
    status_$t local_status;

    /* Acquire exclusion lock: pea (0x520,A5) */
    ML_$EXCLUSION_START(&XNS_IDP_$DATA.lock);

    /* Scan all channels */
    for (chan = 0; chan < XNS_MAX_CHANNELS; chan++) {
        xns_$channel_t *c = &XNS_IDP_$DATA.channels[chan];     /* A3 */

        /* Check if channel is active: tst.w (0xe4,A3) / bpl */
        if (c->state >= 0) {
            continue;  /* Not active */
        }

        /* Check if channel is owned by this AS */
        uint16_t chan_as_id = (c->flags &
                               XNS_CHAN_FLAG_AS_ID_MASK) >> XNS_CHAN_FLAG_AS_ID_SHIFT;
        if (chan_as_id != as_id) {
            continue;  /* Not owned by this AS */
        }

        /* Close user socket if allocated */
        {
            uint16_t user_socket = c->user_socket;
            if (user_socket != XNS_NO_SOCKET) {
                SOCK_$CLOSE(user_socket);
            }
        }

        /* Delete all port bindings */
        for (port = 0; port < XNS_MAX_PORTS; port++) {
            if ((int8_t)c->port_active[port] < 0) {     /* tst.b (0xdc,A0) */
                xns_$delete_port(chan, port, &local_status);
            }
        }

        /* Clear channel state.  bclr.b #7 / andi.b #7 act on the HIGH byte
         * of the state and flags words (bits 15 and 11..15). */
        c->state &= (int16_t)~0x8000;           /* bclr.b #0x7,(0xe4,A3) */
        c->flags &= 0x07FF;                     /* andi.b #0x7,(0xda,A3) */
        c->user_socket = XNS_NO_SOCKET;         /* move.w #0xe1,(0xd6,A3) */
        c->xns_socket = 0;                      /* clr.w (0xd8,A3) */

        /* Decrement open channel count: subq.w #1,(0x534,A5) */
        XNS_IDP_$DATA.open_channels -= 1;
    }

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&XNS_IDP_$DATA.lock);
}
