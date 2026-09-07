/*
 * MSG_$SET_HPIPC - Set HPIPC socket ownership
 *
 * Validates that the current process owns the specified socket.
 * HPIPC = High Performance Inter-Process Communication.
 *
 * Original address: 0x00E59140
 * Original size: 88 bytes
 */

#include "msg/msg_internal.h"

void MSG_$SET_HPIPC(msg_$socket_t *socket, void *param2, status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t sock_num;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;

    (void)param2;  /* Unused in original implementation */

    sock_num = *socket;

    /* Validate socket number */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* Check ownership */
    asid = PROC1_$AS_ID;
    /* base + 0x1D8 + socket*8 - the one-based ownership table. */
    bitmap = MSG_$SOCK_OWNERS[sock_num];
    byte_index = (0x3F - asid) >> 3;

    if ((bitmap[byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    /* Socket ownership verified */
    *status_ret = status_$ok;

#else
    (void)socket;
    (void)param2;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}
