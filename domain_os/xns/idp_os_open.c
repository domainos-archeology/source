/*
 * XNS IDP OS-Level Channel Management
 *
 * Implementation of XNS_IDP_$OS_OPEN and XNS_IDP_$OS_CLOSE for
 * OS-level (kernel-internal) IDP channel management.
 *
 * Original addresses:
 *   XNS_IDP_$OS_OPEN:  0x00E17F02
 *   XNS_IDP_$OS_CLOSE: 0x00E181D8
 *
 * Module data through XNS_IDP_$DATA: Claude Opus 5.5 (source-iq58).
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$OS_OPEN - Open an IDP channel (OS-level), 0x00E17F02
 *
 * Walks the channel table for a free slot, optionally binds the channel to
 * one or every ROUTE port, optionally resolves a connected destination, and
 * finally stamps the socket number, the demux vector and the owning AS_ID
 * into the slot.
 *
 * The option record is xns_$os_open_opt_t; see xns/xns.h for how every field
 * was recovered.  Note that its +0x02 word is an input (the open flags, in
 * its low byte) that becomes an output (the channel index) at 0x00E181A4.
 *
 * @param options       the open options; +0x00 and +0x02 are written back
 * @param status_ret    Output: status code
 */
void XNS_IDP_$OS_OPEN(xns_$os_open_opt_t *options, status_$t *status_ret)
{
    uint8_t   flags;                    /* the byte at options +0x03 */
    /*
     * D2, the channel index.
     *
     * QUIRK, reproduced as found: it is not cleared until 0x00E17F56, yet the
     * "IDP socket in use" exit at 0x00E17F46 branches to 0x00E181A0, which
     * lands in the CLEANUP arm and indexes the channel table with it.  On
     * that one path D2 still holds whatever the CALLER left in it, so the
     * bclr/clr at 0x00E181BA/0x00E181C0 scribble on an unpredictable slot.
     * Leaving `channel' indeterminate here is the faithful reading; nothing
     * in the image constrains the value.
     */
    uint16_t  channel;
    xns_$channel_t *chan;               /* A0 / A4: A5 + channel * 0x48 */
    boolean   use_local_source;         /* D3 from 0x00E17FFC on */
    int16_t   port;                     /* D3 in the bind arm, D4 later */
    int       i;

    /*
     * `channel' is deliberately given the contents of its own unwritten slot,
     * read through a volatile pointer.  That is as close as C gets to "the
     * value the caller left in D2", which is what the socket-in-use arm
     * below actually uses; see the declaration.
     */
    channel = *(volatile uint16_t *)&channel;

    *status_ret = status_$ok;                           /* 0x00E17F18 */

    /* 0x00E17F1A "cmpi.w #0x10,(0x534,A5)" / `bcs' - an UNSIGNED compare. */
    if (XNS_IDP_$DATA.open_channels >= XNS_MAX_CHANNELS) {
        *status_ret = status_$xns_idp_socket_table_full;
        /* 0x00E17F28 "bra.w 0x00E181CE" - straight to the epilogue, because
         * the exclusion lock has not been taken yet. */
        return;
    }

    if (options->socket != 0) {                         /* 0x00E17F2E */
        /* 0x00E17F32 "subq.l #0x2,SP" is the Pascal result slot; the answer
         * also comes back in D0. */
        if (xns_$find_socket(options->socket) < 0) {    /* 0x00E17F3C */
            *status_ret = status_$xns_socket_in_use;
            /*
             * 0x00E17F46 "bra.w 0x00E181A0" - the `bne' there sees the flags
             * of the store just made, so it always falls into the cleanup arm
             * at 0x00E181AA.  That arm ends with an ML_$EXCLUSION_STOP for a
             * lock this path never took.  Both halves are reproduced.
             */
            goto cleanup_error;
        }
    }

    ML_$EXCLUSION_START(&XNS_IDP_$DATA.lock);           /* 0x00E17F4E */

    /*
     * 0x00E17F56-0x00E17F76: find the first slot whose state word is not
     * negative.  The bound check sits INSIDE the loop body, after the state
     * test, so a table that is full to the brim reads the state word of a
     * seventeenth channel - A5 + 0x564, past the end of the 0x53C-byte block,
     * in the image the XNS_IDP_ASM code that follows it - before deciding.
     * QUIRK, reproduced as found: in C that read is channels[16].state, one
     * element past the array (and past XNS_IDP_$DATA).
     */
    channel = 0;                                        /* 0x00E17F56 */
    chan = &XNS_IDP_$DATA.channels[0];                  /* 0x00E17F58 */
    while (chan->state < 0) {                           /* 0x00E17F72 */
        if (channel >= XNS_MAX_CHANNELS) {              /* 0x00E17F5C `bcs' */
            *status_ret = status_$xns_channel_table_full;
            goto cleanup_error;                         /* 0x00E17F68 */
        }
        channel += 1;                                   /* 0x00E17F6C */
        chan += 1;                                      /* 0x00E17F6E lea (0x48,A0) */
    }

    /* 0x00E17F7A "btst.b #0x1,(0x3,A1)" - the flags are the LOW byte of the
     * +0x02 word, and nothing rewrites that word before 0x00E181A4. */
    flags = (uint8_t)options->flags_channel;

    if (flags & XNS_OPEN_FLAG_BIND_LOCAL) {
        if ((int32_t)options->network == -1) {          /* 0x00E17F82 */
            boolean any_bound = false;                  /* 0x00E17F8C `clr.b D3b' */
            int16_t p = 0;                              /* 0x00E17F90 `clr.w D5w' */

            /* 0x00E17F8E "moveq #0x7,D4" + `dbf' at 0x00E17FA6 = 8 passes. */
            for (i = 0; i < XNS_MAX_PORTS; i++) {
                xns_$add_port(channel, p, status_ret);  /* 0x00E17F98 */
                if (*status_ret == status_$ok) {        /* 0x00E17F9E */
                    any_bound = true;                   /* 0x00E17FA2 `st D3b' */
                }
                p += 1;                                 /* 0x00E17FA4 */
            }

            if (any_bound < 0) {                        /* 0x00E17FAA `bpl' */
                /*
                 * 0x00E17FB0 / 0x00E17FB8: at least one port took the
                 * channel, so the two "this port is simply not there" codes
                 * the last port left behind are forgiven.
                 */
                if (*status_ret == status_$internet_network_port_not_open ||
                    *status_ret == status_$mac_port_op_not_implemented) {
                    *status_ret = status_$ok;           /* 0x00E17FC0 */
                    goto after_bind;                    /* 0x00E17FC2 */
                }
            }
        } else {
            int16_t port_num;                           /* A6-0x16 */

            /* 0x00E17FC4 "pea (-0x16,A6)" then "pea (0x8,A1)": the network id
             * is the longword at options +0x08. */
            MAC_$NET_TO_PORT_NUM((int32_t *)&options->network, &port_num);
            port = port_num;                            /* 0x00E17FD4 */
            if (port == -1) {                           /* 0x00E17FD8 */
                *status_ret = status_$xns_listen_network_not_connected;
                goto cleanup_error;
            }
            xns_$add_port(channel, port, status_ret);   /* 0x00E17FEE */
        }

        if (*status_ret != status_$ok) {                /* 0x00E17FF4 */
            goto cleanup_error;                         /* 0x00E17FF6 */
        }
    }

after_bind:
    use_local_source = false;                           /* 0x00E17FFC `clr.b D3b' */

    if (flags & XNS_OPEN_FLAG_CONNECT) {                /* 0x00E17FFE btst #2 */
        int16_t  nexthop_port;                          /* A6-0x16 */
        uint8_t  nexthop[16];                           /* A6-0x10 */
        boolean  arp_is_broadcast;                      /* A6-0x20 */

        /*
         * 0x00E18008-0x00E18024: the all-zero test is on the SOURCE address
         * at options +0x0C - the longword +0x0C, then the words +0x16, +0x10,
         * +0x12 and +0x14, in that order.  An all-zero source means "fill it
         * in for me".
         */
        if (options->src_network == 0 && options->src_socket == 0 &&
            options->src_host_hi == 0 && options->src_host_mid == 0 &&
            options->src_host_lo == 0) {
            use_local_source = true;                    /* 0x00E18026 `st D3b' */
        } else {
            /* 0x00E1802A "pea (0xc,A1)" - again the SOURCE address. */
            if (xns_$is_broadcast_addr(&options->src_network) >= 0) {
                *status_ret = status_$xns_source_must_be_this_node;
                goto cleanup_error;                     /* 0x00E1803E */
            }
        }

        /*
         * 0x00E18042-0x00E1805C.  Pushed right to left: the DESTINATION
         * address at options +0x18, a TRUE byte ("st -(SP)"), &nexthop_port,
         * &nexthop and status_ret, under a word result slot.
         */
        (void)RIP_$FIND_NEXTHOP(&options->dest_network, true, &nexthop_port,
                                nexthop, status_ret);
        port = nexthop_port;                            /* 0x00E18060 */
        if (*status_ret != status_$ok) {                /* 0x00E18064 */
            goto cleanup_error;
        }
        if (port == -1) {                               /* 0x00E1806E */
            *status_ret = status_$xns_network_unreachable;
            goto cleanup_error;
        }

        /*
         * 0x00E1807E-0x00E180A4: resolve the next hop's link address into the
         * channel's own 0x18-byte MAC info block at +0xBC.  The fourth
         * argument is a local broadcast-flag byte MAC_OS_$ARP writes; it is
         * never NULL.
         */
        chan = &XNS_IDP_$DATA.channels[channel];        /* 0x00E18092 A4 */
        MAC_OS_$ARP(nexthop, port, (uint16_t *)chan->mac_info,
                    (uint8_t *)&arp_is_broadcast, status_ret);
        if (*status_ret != status_$ok) {                /* 0x00E180A8 */
            goto cleanup_error;
        }

        xns_$add_port(channel, port, status_ret);       /* 0x00E180B4 */
        if (*status_ret != status_$ok) {                /* 0x00E180BA */
            goto cleanup_error;
        }

        /* 0x00E180C4-0x00E180D2: twelve bytes of destination address, then
         * the port the connection goes out of. */
        for (i = 0; i < 12; i++) {
            ((uint8_t *)&chan->dest_network)[i] =
                ((const uint8_t *)&options->dest_network)[i];
        }
        chan->connected_port = port;

        if (use_local_source < 0) {                     /* 0x00E180D6 */
            /*
             * 0x00E180DA-0x00E180EE: the local network number is the longword
             * the PORT TABLE entry points at - "lea (0,A5,port*0xC),A0 /
             * movea.l (0x44,A0),A3 / move.l (A3),(0xb0,A1)".  A5+0x44+port*12
             * is xns_$port_state_t.net_addr_ptr of that port, NOT
             * ROUTE_$PORTP[port]->network.
             */
            uint32_t net_va = XNS_IDP_$DATA.ports[port].net_addr_ptr;
            const uint32_t *net_ptr = (const uint32_t *)ARCH_VA_TO_PTR(net_va);

            chan->src_network = *net_ptr;

            /*
             * 0x00E180F2-0x00E18108: three words from the state's first
             * registered address (A5+0x20) into the channel's source host.
             */
            for (i = 0; i < 6; i++) {
                chan->src_host[i] = ((const uint8_t *)XNS_IDP_$DATA.addrs[0])[i];
            }

            /*
             * 0x00E1810A "move.w (0xd8,A1),(0xba,A1)".  QUIRK, reproduced as
             * found: the channel's XNS socket is not written until
             * 0x00E1816A, so this copies the value LEFT OVER from whoever
             * used the slot last, not the socket about to be assigned.
             */
            chan->src_port = (uint16_t)chan->xns_socket;
        } else {
            /* 0x00E18112-0x00E18120: twelve bytes of caller-supplied source. */
            for (i = 0; i < 12; i++) {
                ((uint8_t *)&chan->src_network)[i] =
                    ((const uint8_t *)&options->src_network)[i];
            }
        }
    }

    if (options->socket == 0) {                         /* 0x00E18124 */
        /*
         * 0x00E18128-0x00E1814C.  The socket handed out is the CURRENT value
         * of the allocator, which the PREVIOUS call already checked; the loop
         * then advances past every socket that is in use so the next caller
         * gets a free one.
         */
        options->socket = (int16_t)XNS_IDP_$DATA.next_socket;   /* 0x00E18128 */
        do {
            XNS_IDP_$DATA.next_socket += 1;                     /* 0x00E1812C */
            /* 0x00E18130 "cmpi.w #-0x2,(0x536,A5)" / `bls' - an UNSIGNED
             * compare against 0xFFFE, so the wrap happens only at 0xFFFF. */
            if (XNS_IDP_$DATA.next_socket > 0xFFFE) {
                XNS_IDP_$DATA.next_socket = XNS_FIRST_DYNAMIC_PORT;   /* 0x00E18138 */
            }
        } while (xns_$find_socket((int16_t)XNS_IDP_$DATA.next_socket) < 0); /* 0x00E1814C */
    }

    XNS_IDP_$DATA.open_channels += 1;                   /* 0x00E1814E */

    chan = &XNS_IDP_$DATA.channels[channel];            /* 0x00E1815E */

    /* 0x00E18162 "bset.b #0x7,(0xe4,A0)" - a byte operation on the HIGH half
     * of the state word, i.e. bit 15.  Written as a word mask so the host
     * build touches the same bit. */
    chan->state |= (int16_t)0x8000;

    chan->xns_socket = options->socket;                 /* 0x00E1816A */
    chan->user_socket = XNS_NO_SOCKET;                  /* 0x00E1816E */

    /* 0x00E18174 "move.l (0x4,A1),(0xa0,A0)" - one longword, the code
     * address the option record carries. */
    chan->demux = (code_ptr_t)(uintptr_t)options->demux;

    /*
     * 0x00E1817A-0x00E1819A.  The first three operations are BYTE operations
     * on the HIGH half of the flags word, so open-flag bit n ends up in word
     * bit n+11; the AS_ID then goes into bits 5..10 with word operations.
     */
    {
        uint16_t *chan_flags = &chan->flags;
        uint8_t   as_id = (uint8_t)PROC1_$AS_ID;        /* 0x00E1818C */

        *chan_flags &= 0x07FF;                          /* 0x00E1817A andi.b #7 */
        *chan_flags |= (uint16_t)(((uint16_t)(uint8_t)(flags << 3)) << 8);
        *chan_flags &= (uint16_t)~XNS_CHAN_FLAG_AS_ID_MASK;  /* 0x00E18192 */
        *chan_flags |= (uint16_t)(as_id << XNS_CHAN_FLAG_AS_ID_SHIFT);
    }

    if (*status_ret != status_$ok) {                    /* 0x00E1819E */
        goto cleanup_error;
    }

    /* 0x00E181A4 "move.w D2w,(0x2,A0)" - the whole word, flags included. */
    options->flags_channel = channel;

    ML_$EXCLUSION_STOP(&XNS_IDP_$DATA.lock);            /* 0x00E181C8 */
    return;

cleanup_error:
    /* 0x00E181AA-0x00E181C0 */
    chan = &XNS_IDP_$DATA.channels[channel];
    /* 0x00E181BA "bclr.b #0x7,(0xe4,A0)" - again bit 15 of the state word. */
    chan->state &= (int16_t)~0x8000;
    chan->demux = NULL;                                 /* clr.l (0xa0,A0) */
    ML_$EXCLUSION_STOP(&XNS_IDP_$DATA.lock);            /* 0x00E181C8 */
}

/*
 * XNS_IDP_$OS_CLOSE - Close an IDP channel (OS-level)
 *
 * Closes a previously opened IDP channel and releases all resources.
 * This includes:
 *   1. Removing all port bindings
 *   2. Clearing channel state
 *   3. Decrementing open channel count
 *
 * @param channel_ptr   Pointer to channel number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E181D8
 */
void XNS_IDP_$OS_CLOSE(int16_t *channel_ptr, status_$t *status_ret)
{
    int16_t channel = *channel_ptr;
    xns_$channel_t *chan;
    int16_t port;

    *status_ret = status_$ok;

    /* Acquire exclusion lock: pea (0x520,A5) */
    ML_$EXCLUSION_START(&XNS_IDP_$DATA.lock);

    /* Decrement open channel count: (0x534,A5) */
    XNS_IDP_$DATA.open_channels -= 1;

    chan = &XNS_IDP_$DATA.channels[channel];

    /* Delete all active port bindings */
    for (port = 0; port < XNS_MAX_PORTS; port++) {
        if ((int8_t)chan->port_active[port] < 0) {
            xns_$delete_port(channel, port, status_ret);
        }
    }

    /* Clear channel state.  The byte operations act on the HIGH byte of the
     * state and flags words (bit 15; bits 11..15). */
    chan->state &= (int16_t)~0x8000;     /* Clear active flag */
    chan->flags &= 0x07FF;               /* Clear flags */
    chan->xns_socket = 0;                /* Clear socket */
    chan->demux = NULL;                  /* Clear callback */

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&XNS_IDP_$DATA.lock);
}
