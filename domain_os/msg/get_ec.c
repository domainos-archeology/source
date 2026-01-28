/*
 * MSG_$GET_EC - Get event count for socket
 *
 * Returns a registered event count for the specified socket.
 * The event count can be used for waiting on messages.
 *
 * Original address: 0x00E59CDA
 * Original size: 120 bytes
 */

#include "msg/msg_internal.h"

#define EC_$SOCK_TABLE      0xE28DB0    /* Socket event count table */

void MSG_$GET_EC(msg_$socket_t *socket, uint32_t *ec, status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t sock_num;
    int16_t sock_offset;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;
    void *sock_ec;

    sock_num = *socket;

    /* Validate socket number */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* Check ownership */
    asid = PROC1_$AS_ID;
    sock_offset = sock_num << 3;
    bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + sock_offset);
    byte_index = (0x3F - asid) >> 3;

    if ((bitmap[byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    /*
     * Get socket's event count and register it.
     * EC2_$REGISTER_EC1 returns a user-accessible event count handle.
     */
    sock_ec = *(void **)(EC_$SOCK_TABLE + sock_num * 4);
    *ec = (uint32_t)EC2_$REGISTER_EC1(sock_ec, status_ret);

#else
    (void)socket;
    (void)ec;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}
