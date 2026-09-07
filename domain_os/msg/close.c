/*
 * MSG_$CLOSE, MSG_$CLOSEI - Close a message socket
 *
 * Closes a message socket for the current process.
 * If this was the last owner, the underlying socket is freed.
 *
 * Original addresses:
 *   MSG_$CLOSE:  0x00E593D2 (18 bytes)
 *   MSG_$CLOSEI: 0x00E593E4 (270 bytes)
 */

#include "msg/msg_internal.h"

/* MSG_$NET_SERVICE_CLOSE (op code word at 0x00E594F2) is in msg/msg_internal.h */

/*
 * MSG_$CLOSEI - Close socket internal implementation
 */
/* TODO(source-yo76): ARCH_M68K-only body with a fabricated #else status. */
void MSG_$CLOSEI(msg_$socket_t *socket, status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t sock_num;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t bit_mask;
    uint8_t *bitmap;
    uint8_t my_ownership[8];
    uint8_t new_ownership[8];
    int i;
    status_$t net_status;
    uint32_t service_type;

    sock_num = *socket;

    /* Validate socket number (1-224) */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* Lock the socket table */
    ML_$EXCLUSION_START((void *)MSG_$SOCK_LOCK);

    bitmap = MSG_$SOCK_OWNERS[sock_num];   /* base + 0x1D8 + socket*8 */

    /*
     * Check if current ASID owns this socket.
     */
    asid = PROC1_$AS_ID;
    byte_index = (0x3F - asid) >> 3;
    bit_mask = 1 << (asid & 7);

    if ((bitmap[byte_index] & bit_mask) == 0) {
        /* Current process doesn't own this socket */
        *status_ret = status_$msg_no_owner;
        ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
        return;
    }

    /*
     * Build bitmap with only current ASID's bit set.
     */
    for (i = 0; i < 8; i++) {
        my_ownership[i] = 0;
    }
    my_ownership[byte_index] |= bit_mask;

    /*
     * Clear current ASID's ownership: new = old & ~my_ownership
     */
    for (i = 0; i < 8; i++) {
        new_ownership[i] = bitmap[i] & ~my_ownership[i];
    }

    /* Store new ownership */
    for (i = 0; i < 8; i++) {
        bitmap[i] = new_ownership[i];
    }

    /*
     * Check if all owners are gone (bitmap is all zeros).
     */
    if (*(uint32_t *)bitmap == 0 && *(uint32_t *)(bitmap + 4) == 0) {
        /* Decrement open socket count */
        MSG_$DATA->open_count--;               /* 0x00E5949E */

        /* Close the underlying socket */
        SOCK_$CLOSE(sock_num);

        /* If no more sockets open, unregister network service */
        if (MSG_$DATA->open_count == 0) {       /* 0x00E594AE */
            /* Clear user socket open flag */
            NETWORK_$USER_SOCK_OPEN = 0;        /* 0x00E594B4 "clr.b" */

            /* Unregister network service */
            service_type = 0x80000;
            NETWORK_$SET_SERVICE((int16_t *)&MSG_$NET_SERVICE_CLOSE, &service_type, &net_status);
        }
    }

    ML_$EXCLUSION_STOP((void *)MSG_$SOCK_LOCK);
    *status_ret = status_$ok;

#else
    (void)socket;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}

/*
 * MSG_$CLOSE - Close socket wrapper
 */
void MSG_$CLOSE(msg_$socket_t *socket, status_$t *status_ret)
{
    status_$t status;

    MSG_$CLOSEI(socket, &status);
    *status_ret = status;
}
