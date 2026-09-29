/*
 * XNS IDP User-Level Channel Management
 *
 * Implementation of XNS_IDP_$OPEN and XNS_IDP_$CLOSE for
 * user-level IDP channel management.
 *
 * Original addresses:
 *   XNS_IDP_$OPEN:  0x00E187AC
 *   XNS_IDP_$CLOSE: 0x00E189C4
 *
 * Module data through XNS_IDP_$DATA / XNS_ERROR_$DATA: Claude Opus 5.5
 * (source-iq58).
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$OPEN - Open an IDP channel (user-level), 0x00E187AC
 *
 * Validates the caller's request, allocates an OS socket for it, builds the
 * xns_$os_open_opt_t record XNS_IDP_$OS_OPEN wants, and publishes the
 * socket's event count back to the caller.
 *
 * @param options       xns_$idp_open_opt_t; +0x04 and +0x08 are OUTPUTS on
 *                      the way back (see xns/xns.h)
 * @param status_ret    Output: status code
 */
void XNS_IDP_$OPEN(xns_$idp_open_opt_t *options, status_$t *status_ret)
{
    uint16_t  user_socket;              /* D2 */
    uint16_t  channel;
    xns_$channel_t *chan;
    sock_$sock_t *sock;                 /* A4 */
    xns_$os_open_opt_t os_opt;          /* A6-0x28 */
    status_$t os_status;                /* A6-0x2C */
    int i;

    *status_ret = status_$ok;                           /* 0x00E187C2 */

    if (options->version != 1) {                        /* 0x00E187C4 */
        *status_ret = status_$xns_version_mismatch;
        return;
    }

    /* 0x00E187D4-0x00E187EE: the four sockets user code may not claim. */
    if (options->socket == -1 || options->socket == XNS_SOCKET_ROUTER ||
        options->socket == XNS_SOCKET_ERROR || options->socket == XNS_SOCKET_RIP) {
        *status_ret = status_$xns_reserved_socket;
        return;
    }

    if (options->socket != 0) {                         /* 0x00E187FA */
        if (xns_$find_socket(options->socket) < 0) {    /* 0x00E1880C */
            *status_ret = status_$xns_socket_in_use;
            return;
        }
    }

    if (options->flags & XNS_OPEN_FLAG_BIND_LOCAL) {    /* 0x00E1881A */
        if (options->flags & XNS_OPEN_FLAG_NO_ALLOC) {  /* 0x00E18822 */
            *status_ret = status_$xns_incompatible_flags;
            return;
        }
        if (options->flags & XNS_OPEN_FLAG_CONNECT) {   /* 0x00E18834 */
            *status_ret = status_$xns_connect_bind_conflict;
            return;
        }
    }

    if (options->flags & XNS_OPEN_FLAG_CONNECT) {       /* 0x00E18846 */
        if (options->flags & XNS_OPEN_FLAG_NO_ALLOC) {  /* 0x00E1884E */
            *status_ret = status_$xns_incompatible_flags2;
            return;
        }
        /*
         * 0x00E18860-0x00E1888E: an all-ones DESTINATION host (+0x18..+0x1D)
         * is rejected, and so is an all-ones SOURCE host (+0x0C..+0x11).  The
         * words are compared in the order +0x1C, +0x18, +0x1A then +0x10,
         * +0x0C, +0x0E.
         */
        if ((options->dest_host_lo == 0xFFFF &&
             options->dest_host_hi == 0xFFFF &&
             options->dest_host_mid == 0xFFFF) ||
            (options->src_host_lo == 0xFFFF &&
             options->src_host_hi == 0xFFFF &&
             options->src_host_mid == 0xFFFF)) {
            *status_ret = status_$xns_connect_to_broadcast;
            return;
        }
    }

    if (options->flags & XNS_OPEN_FLAG_NO_ALLOC) {      /* 0x00E18892 */
        user_socket = XNS_NO_SOCKET;                    /* 0x00E1889A */
    } else {
        if (options->buffer_size == 0) {                /* 0x00E188A0 */
            *status_ret = status_$xns_no_buffer_size;   /* 0x00E188EE */
            return;
        }

        /*
         * 0x00E188A6-0x00E188BC.  The pushes are 0x400, buffer_size, and then
         * TWO copies of the word already on the stack ("move.w (SP),-(SP)"
         * twice), so SOCK_$ALLOCATE_USER sees the depth three times.
         */
        if (SOCK_$ALLOCATE_USER(&user_socket, options->buffer_size,
                                options->buffer_size, options->buffer_size,
                                0x400) >= 0) {          /* 0x00E188C4 `bmi' */
            *status_ret = status_$xns_no_os_sockets;
            return;
        }

        /*
         * 0x00E188D2-0x00E188E6.  SOCK_$SOCKET_PTR is a 1-BASED array of
         * socket-descriptor pointers at 0x00E28DB4: the index arithmetic is
         * "A0 = 0xE28DB4 / D0 = sock << 2 / A1 = A0 + D0 / A4 = (-0x4,A1)",
         * i.e. SOCK_$SOCKET_PTR[sock - 1].  "bclr.b #0x7,(0x16,A4)" then
         * clears bit 15 of that descriptor's flags word.
         */
        sock = (sock_$sock_t *)SOCK_$SOCKET_PTR[user_socket - 1];
        sock->flags &= (uint16_t)~0x8000u;
    }

    /* 0x00E188F8-0x00E1892E: build the OS-level record. */
    os_opt.socket = options->socket;                    /* 0x00E188F8 */
    os_opt.demux = (uint32_t)(uintptr_t)&XNS_IDP_$DEMUX; /* 0x00E188FE */
    /* 0x00E18906 "move.w (0x20,A2),(-0x26,A6)" - the whole word, whose low
     * byte is the open-flag byte XNS_IDP_$OS_OPEN reads back. */
    os_opt.flags_channel = (uint16_t)(((uint16_t)options->flags_hi << 8) |
                                      options->flags);

    if (options->flags & XNS_OPEN_FLAG_BIND_LOCAL) {    /* 0x00E1890C */
        os_opt.network = options->network;              /* 0x00E18914 */
    }

    if (options->flags & XNS_OPEN_FLAG_CONNECT) {       /* 0x00E1891A */
        /*
         * 0x00E18922 "lea (0x8,A2),A0 / lea (-0x1c,A6),A1 / moveq #0x17,D0 /
         * move.b (A0)+,(A1)+ / dbf" - 24 bytes from the caller's +0x08 onto
         * the OS record's +0x0C, i.e. source address then destination
         * address.
         */
        const uint8_t *src = (const uint8_t *)&options->channel_ret;
        uint8_t *dst = (uint8_t *)&os_opt.src_network;

        for (i = 0; i < 24; i++) {
            dst[i] = src[i];
        }
    }

    XNS_IDP_$OS_OPEN(&os_opt, &os_status);              /* 0x00E1893A */
    *status_ret = os_status;                            /* 0x00E18940 */

    if (os_status != status_$ok) {                      /* 0x00E18944 */
        if (user_socket != XNS_NO_SOCKET) {             /* 0x00E18946 */
            SOCK_$CLOSE(user_socket);                   /* 0x00E18950 */
        }
        return;
    }

    PROC2_$SET_CLEANUP(0x0E);                           /* 0x00E1895E */

    /*
     * 0x00E18966-0x00E18990.  The channel index the OS record now carries is
     * turned into a channel base with WORD arithmetic ("lsl.w #0x3" twice and
     * an "add.w"), so the multiply is modulo 65536; XNS_IDP_$OS_OPEN hands
     * back 0..15, for which that is simply A5 + channel * 0x48.
     */
    channel = os_opt.flags_channel;
    chan = &XNS_IDP_$DATA.channels[channel];

    ML_$EXCLUSION_START(&XNS_IDP_$DATA.lock);           /* 0x00E1896A */
    chan->user_socket = user_socket;                    /* 0x00E18982 */
    ML_$EXCLUSION_STOP(&XNS_IDP_$DATA.lock);            /* 0x00E1898A */

    /*
     * 0x00E18992 "move.w (-0x26,A6),(0x8,A2)" - a WORD, into the caller's
     * +0x08.  That word is the high half of the source network the caller
     * supplied, so a connected open gets its source network clobbered with
     * the channel index on the way out.
     */
    options->channel_ret = (int16_t)channel;

    /*
     * 0x00E18998-0x00E189B6.  QUIRK, reproduced as found: this is
     * UNCONDITIONAL.  When the NO_ALLOC arm ran, user_socket is 0xE1 and the
     * index arithmetic reaches SOCK_$SOCKET_PTR[0xE0], well past the 0xE0
     * descriptors SOCK_$INIT sets up, so an event count is registered on
     * whatever pointer happens to sit there.
     */
    options->network = (uint32_t)(uintptr_t)
        EC2_$REGISTER_EC1((ec_$eventcount_t *)SOCK_$SOCKET_PTR[user_socket - 1],
                          status_ret);
}

/*
 * XNS_IDP_$CLOSE - Close an IDP channel (user-level)
 *
 * Closes a previously opened user-level IDP channel. This:
 *   1. Validates ownership
 *   2. Closes the user socket
 *   3. Calls XNS_IDP_$OS_CLOSE
 *
 * @param channel_ptr   Pointer to channel number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E189C4
 */
void XNS_IDP_$CLOSE(uint16_t *channel_ptr, status_$t *status_ret)
{
    uint16_t channel = *channel_ptr;
    xns_$channel_t *chan;
    status_$t local_status;

    *status_ret = status_$ok;

    /* Acquire exclusion lock: pea (0x520,A5) */
    ML_$EXCLUSION_START(&XNS_IDP_$DATA.lock);

    /* Validate channel number and ownership */
    if (channel >= XNS_MAX_CHANNELS) {
        goto bad_channel;
    }

    chan = &XNS_IDP_$DATA.channels[channel];

    /* Check channel is active */
    if (chan->state >= 0) {
        goto bad_channel;
    }

    /* Check ownership (AS_ID must match) */
    {
        uint16_t chan_as_id = (chan->flags &
                               XNS_CHAN_FLAG_AS_ID_MASK) >> XNS_CHAN_FLAG_AS_ID_SHIFT;
        if (chan_as_id != PROC1_$AS_ID) {
            goto bad_channel;
        }
    }

    /* Close user socket if allocated */
    {
        uint16_t user_socket = chan->user_socket;
        if (user_socket != XNS_NO_SOCKET) {
            SOCK_$CLOSE(user_socket);
        }
        chan->user_socket = XNS_NO_SOCKET;
    }

    /* Release lock before calling OS_CLOSE */
    ML_$EXCLUSION_STOP(&XNS_IDP_$DATA.lock);

    /* Call OS-level close */
    XNS_IDP_$OS_CLOSE((int16_t *)channel_ptr, &local_status);
    *status_ret = local_status;
    return;

bad_channel:
    ML_$EXCLUSION_STOP(&XNS_IDP_$DATA.lock);
    *status_ret = status_$xns_bad_channel;
}
