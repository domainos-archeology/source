/*
 * MSG_$TEST_FOR_MESSAGE - Test if message is available on socket
 *
 * Non-blocking check for message availability.
 * Returns the socket's event count and whether a message is pending.
 *
 * Original address: 0x00E59FB2
 * Original size: 122 bytes
 */

#include "msg/msg_internal.h"

#define EC_$SOCK_TABLE      0xE28DB0    /* Socket event count table */

int16_t MSG_$TEST_FOR_MESSAGE(msg_$socket_t *socket, uint32_t *ec_value,
                               status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t sock_num;
    int16_t sock_offset;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;
    void *sock_ec;
    uint8_t pending;

    sock_num = *socket;

    /* Validate socket number */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return 0;
    }

    /* Check ownership */
    asid = PROC1_$AS_ID;
    sock_offset = sock_num << 3;
    bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + sock_offset);
    byte_index = (0x3F - asid) >> 3;

    if ((bitmap[byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return 0;
    }

    /*
     * Get socket's event count.
     */
    sock_ec = *(void **)(EC_$SOCK_TABLE + sock_num * 4);

    /* Return current event count value */
    *ec_value = *(uint32_t *)sock_ec;

    *status_ret = status_$ok;

    /*
     * Check if message is pending (byte at offset 0x15).
     * Return -1 if message pending, 0 if not.
     */
    pending = *(uint8_t *)((uint8_t *)sock_ec + 0x15);
    return (pending != 0) ? -1 : 0;

#else
    (void)socket;
    (void)ec_value;
    *status_ret = status_$msg_socket_out_of_range;
    return 0;
#endif
}
