/*
 * XNS IDP User-Level Channel Management
 *
 * Implementation of XNS_IDP_$OPEN and XNS_IDP_$CLOSE for
 * user-level IDP channel management.
 *
 * Original addresses:
 *   XNS_IDP_$OPEN:  0x00E187AC
 *   XNS_IDP_$CLOSE: 0x00E189C4
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$OPEN - Open an IDP channel (user-level)
 *
 * Opens a new IDP channel for user-mode communication. This is a
 * higher-level wrapper around XNS_IDP_$OS_OPEN that:
 *   1. Validates the version field
 *   2. Validates socket number isn't reserved
 *   3. Validates flag combinations
 *   4. Allocates a user socket
 *   5. Calls XNS_IDP_$OS_OPEN
 *   6. Registers an event count
 *
 * @param options       Pointer to open options structure (xns_$idp_open_opt_t)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E187AC
 */
void XNS_IDP_$OPEN(xns_$idp_open_opt_t *options, status_$t *status_ret)
{
    uint8_t *base = XNS_IDP_BASE;
    int16_t socket = options->socket;
    uint8_t flags = options->flags;
    uint16_t user_socket;
    int16_t channel;
    status_$t local_status;

    /* OS-level open parameters */
    struct {
        int16_t socket;
        int16_t channel_ret;
        code_ptr_t demux_callback;
        void *user_data;
        uint8_t dest_addr[24];
    } os_open_opt;

    *status_ret = status_$ok;

    /* Validate version field */
    if (options->version != 1) {
        *status_ret = status_$xns_version_mismatch;
        return;
    }

    /* Validate socket number isn't reserved */
    if (socket == -1 || socket == XNS_SOCKET_ROUTER ||
        socket == XNS_SOCKET_ERROR || socket == XNS_SOCKET_RIP) {
        *status_ret = status_$xns_reserved_socket;
        return;
    }

    /* Check if socket is already in use (if non-zero) */
    if (socket != 0) {
        int8_t in_use = xns_$find_socket(socket);
        if (in_use < 0) {
            *status_ret = status_$xns_socket_in_use;
            return;
        }
    }

    /* Validate flag combinations */
    if (flags & XNS_OPEN_FLAG_BIND_LOCAL) {
        if (flags & XNS_OPEN_FLAG_NO_ALLOC) {
            *status_ret = status_$xns_incompatible_flags;
            return;
        }
        if (flags & XNS_OPEN_FLAG_CONNECT) {
            *status_ret = status_$xns_connect_bind_conflict;
            return;
        }
    }

    if (flags & XNS_OPEN_FLAG_CONNECT) {
        if (flags & XNS_OPEN_FLAG_NO_ALLOC) {
            *status_ret = status_$xns_incompatible_flags2;
            return;
        }

        /* Connected mode requires non-broadcast destination */
        if ((options->dest_host_hi == 0xFFFF &&
             options->dest_host_mid == 0xFFFF &&
             options->dest_host_lo == 0xFFFF) ||
            (options->src_host_hi == 0xFFFF &&
             options->src_host_mid == 0xFFFF &&
             options->src_host_lo == 0xFFFF)) {
            *status_ret = status_$xns_broadcast_no_addr;
            return;
        }
    }

    /* Allocate user socket (unless NO_ALLOC flag) */
    if (!(flags & XNS_OPEN_FLAG_NO_ALLOC)) {
        if (options->buffer_size == 0) {
            *status_ret = status_$xns_no_buffer_size;
            return;
        }

        int8_t result = SOCK_$ALLOCATE_USER(&user_socket,
                                             options->buffer_size,
                                             options->buffer_size,
                                             options->buffer_size,
                                             0x400);
        if (result >= 0) {
            *status_ret = status_$xns_socket_already_open;
            return;
        }

        /* Clear the "in use" flag on the socket */
        /* sock_spinlock[user_socket]->flags &= 0x7F */
        /* TODO: This needs the sock structure definition */
    } else {
        user_socket = XNS_NO_SOCKET;
    }

    /* Set up OS-level open parameters */
    os_open_opt.socket = socket;
    os_open_opt.demux_callback = (code_ptr_t)XNS_IDP_$DEMUX;
    os_open_opt.channel_ret = options->priority;

    if (flags & XNS_OPEN_FLAG_BIND_LOCAL) {
        os_open_opt.user_data = options->user_data;
    }

    if (flags & XNS_OPEN_FLAG_CONNECT) {
        /* Copy destination address (24 bytes starting at dest_network) */
        uint8_t *src = (uint8_t *)&options->dest_network;
        uint8_t *dst = os_open_opt.dest_addr;
        int16_t i;
        for (i = 0; i < 24; i++) {
            dst[i] = src[i];
        }
    }

    /* Call OS-level open */
    XNS_IDP_$OS_OPEN(&os_open_opt, &local_status);
    *status_ret = local_status;

    if (local_status != status_$ok) {
        /* Clean up on error */
        if (user_socket != XNS_NO_SOCKET) {
            SOCK_$CLOSE(user_socket);
        }
        return;
    }

    /* Register cleanup handler */
    PROC2_$SET_CLEANUP(0x0E);  /* XNS cleanup type */

    /* Store user socket in channel state */
    channel = os_open_opt.channel_ret;
    {
        uint16_t iVar1 = channel * XNS_CHANNEL_SIZE;

        ML_$EXCLUSION_START((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
        *(uint16_t *)(base + iVar1 + XNS_CHAN_OFF_USER_SOCKET) = user_socket;
        ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
    }

    /* Return channel index */
    options->dest_network = channel;  /* Reuse dest_network field for return */

    /* Register event count */
    if (user_socket != XNS_NO_SOCKET) {
        /* TODO: EC2_$REGISTER_EC1 call */
        options->user_data = EC2_$REGISTER_EC1(NULL /* sock_spinlock[user_socket] */, status_ret);
    }
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
    uint8_t *base = XNS_IDP_BASE;
    uint16_t channel = *channel_ptr;
    int iVar1;
    status_$t local_status;

    *status_ret = status_$ok;

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START((ml_$exclusion_t *)(base + XNS_OFF_LOCK));

    /* Validate channel number and ownership */
    if (channel >= XNS_MAX_CHANNELS) {
        goto bad_channel;
    }

    iVar1 = channel * XNS_CHANNEL_SIZE;

    /* Check channel is active */
    if (*(int16_t *)(base + iVar1 + XNS_CHAN_OFF_STATE) >= 0) {
        goto bad_channel;
    }

    /* Check ownership (AS_ID must match) */
    {
        uint16_t chan_as_id = (*(uint16_t *)(base + iVar1 + XNS_CHAN_OFF_FLAGS) &
                               XNS_CHAN_FLAG_AS_ID_MASK) >> XNS_CHAN_FLAG_AS_ID_SHIFT;
        if (chan_as_id != PROC1_$AS_ID) {
            goto bad_channel;
        }
    }

    /* Close user socket if allocated */
    {
        uint16_t user_socket = *(uint16_t *)(base + iVar1 + XNS_CHAN_OFF_USER_SOCKET);
        if (user_socket != XNS_NO_SOCKET) {
            SOCK_$CLOSE(user_socket);
        }
        *(uint16_t *)(base + iVar1 + XNS_CHAN_OFF_USER_SOCKET) = XNS_NO_SOCKET;
    }

    /* Release lock before calling OS_CLOSE */
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));

    /* Call OS-level close */
    XNS_IDP_$OS_CLOSE((int16_t *)channel_ptr, &local_status);
    *status_ret = local_status;
    return;

bad_channel:
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(base + XNS_OFF_LOCK));
    *status_ret = status_$xns_bad_channel;
}
