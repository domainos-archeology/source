/*
 * MSG_$WAIT, MSG_$WAITI - Wait for message on socket
 *
 * Waits for a message to arrive on the specified socket.
 * Uses event counts to implement the wait with timeout.
 *
 * Original addresses:
 *   MSG_$WAIT:  0x00E59BA4 (28 bytes)
 *   MSG_$WAITI: 0x00E59BC0 (282 bytes)
 */

#include "msg/msg_internal.h"

/*
 * External event count structures
 */
#define EC_$SOCK_TABLE      0xE28DB0    /* Socket event count table */
#define TIME_$CLOCKH        0xE2B0D4    /* High-resolution clock event count */
#define FIM_$QUIT_EC        0xE22002    /* Per-ASID quit event counts */
#define FIM_$QUIT_VALUE     0xE222BA    /* Per-ASID quit values */

/*
 * MSG_$WAITI - Wait for message internal implementation
 */
void MSG_$WAITI(msg_$socket_t *socket, msg_$time_t *timeout, status_$t *status_ret)
{
#if defined(M68K)
    int16_t sock_num;
    int16_t sock_offset;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;
    void *sock_ec;          /* Socket event count */
    uint32_t *ecs[3];       /* Event counts to wait on */
    uint32_t targets[3];    /* Target values for wait */
    int16_t result;

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
     * Get socket's event count from socket table.
     * The socket EC table is at 0xE28DB0, indexed by socket * 4.
     */
    sock_ec = *(void **)(EC_$SOCK_TABLE + sock_num * 4);

    /*
     * Check if message is already available.
     * Byte at offset 0x15 in socket EC struct indicates pending message.
     */
    if (*(uint8_t *)((uint8_t *)sock_ec + 0x15) != 0) {
        /* Message already available */
        *status_ret = status_$ok;
        return;
    }

    /*
     * Set up event count array for EC_$WAIT:
     * - ecs[0] = Socket event count
     * - ecs[1] = Time clock event count
     * - ecs[2] = Quit event count for current ASID
     */
    ecs[0] = (uint32_t *)sock_ec;
    ecs[1] = (uint32_t *)TIME_$CLOCKH;
    ecs[2] = (uint32_t *)(FIM_$QUIT_EC + asid * 12);

    /*
     * Set up target values:
     * - Wait for socket EC to increment
     * - Wait for timeout (clock + timeout value)
     * - Wait for quit signal
     */
    targets[0] = *(uint32_t *)sock_ec + 1;
    targets[1] = *(uint32_t *)TIME_$CLOCKH + timeout->seconds;
    targets[2] = *(uint32_t *)(FIM_$QUIT_EC + asid * 12) + 1;

    /*
     * Wait on all three event counts.
     * Returns index of which event count was triggered.
     */
    result = EC_$WAIT(ecs, targets);

    if (result == 0) {
        /* Socket message arrived */
        *status_ret = status_$ok;
    } else if (result == 1) {
        /* Timeout */
        *status_ret = status_$msg_time_out;
    } else if (result == 2) {
        /* Quit signal - save quit value */
        *(uint32_t *)(FIM_$QUIT_VALUE + asid * 4) =
            *(uint32_t *)(FIM_$QUIT_EC + asid * 12);
        *status_ret = status_$msg_quit_fault;
    }

#else
    (void)socket;
    (void)timeout;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}

/*
 * MSG_$WAIT - Wait for message wrapper
 */
int8_t MSG_$WAIT(msg_$socket_t *socket, msg_$time_t *timeout, status_$t *status_ret)
{
    status_$t status;

    MSG_$WAITI(socket, timeout, &status);
    *status_ret = status;
    return (status == status_$ok) ? -1 : 0;
}
