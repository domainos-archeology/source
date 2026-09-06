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

#include "fim/fim.h"
#include "msg/msg_internal.h"
#include "time/time.h"

/*
 * Socket event count table (0xE28DB0): one longword pointer per socket.
 * 00e59c12  movea.l #0xe28db4,A1
 * 00e59c1a  lea (0x0,A1,D2w*0x1),A3   ; D2 = socket * 4
 * 00e59c1e  movea.l (-0x4,A3),A3      ; A3 = SOCK_EC_TABLE[socket]
 */
#define EC_$SOCK_TABLE      0xE28DB0    /* Socket event count table */

/*
 * MSG_$WAITI - Wait for message internal implementation
 *
 * timeout points at a 16-bit tick count: the original only reads a word
 * from it (00e59c4c clr.l D1 / 00e59c4e move.w (A4),D1w) and adds it to
 * TIME_$CLOCKH to form the timeout deadline.
 */
void MSG_$WAITI(msg_$socket_t *socket, int16_t *timeout, status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t sock_num;
    int16_t sock_offset;
    uint16_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;
    ec_$eventcount_t *sock_ec;  /* Socket event count */
    int32_t sock_target;        /* Wait value for the socket EC */
    int32_t quit_target;        /* Wait value for the quit EC */
    int16_t result;

    sock_num = *socket;

    /* Validate socket number (00e59bd6 - 00e59be6) */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* Check ownership (00e59bea - 00e59c0c) */
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
    sock_ec = *(ec_$eventcount_t **)(EC_$SOCK_TABLE + sock_num * 4);

    /*
     * Both wait targets are computed before the "message already there"
     * test in the original (00e59c22 / 00e59c32).
     */
    sock_target = sock_ec->value + 1;               /* 00e59c22, 00e59c24 */
    quit_target = FIM_$QUIT_VALUE[asid] + 1;        /* 00e59c32, 00e59c36 */

    /*
     * Check if message is already available.
     * Byte at offset 0x15 in socket EC struct indicates pending message.
     * (00e59c3e move.b (0x15,A3),D1b)
     */
    if (*((uint8_t *)sock_ec + 0x15) != 0) {
        /* Message already available */
        *status_ret = status_$ok;
        return;
    }

    /*
     * Wait on all three event counts (00e59c46 - 00e59c7a).  Arguments are
     * pushed right-to-left, so the values go down first and the pointers
     * end up at the lower addresses:
     *   00e59c46  move.l D0,-(SP)              vals[2] = FIM_$QUIT_VALUE[asid]+1
     *   00e59c56  move.l D1,-(SP)              vals[1] = TIME_$CLOCKH + *timeout
     *   00e59c58  move.l D2,-(SP)              vals[0] = sock_ec->value + 1
     *   00e59c6e  pea (0x0,A1,D3w)  A1=0xe22002, D3=asid*12
     *                                          ecs[2] = &FIM_$QUIT_EC[asid]
     *   00e59c72  move.l #0xe2b0d4,-(SP)       ecs[1] = &TIME_$CLOCKH
     *   00e59c78  pea (A3)                     ecs[0] = sock_ec
     * Returns the 0-based index of the event count that was satisfied.
     */
    result = EC_$WAIT(
        (ec_$wait_ecs_t){{ sock_ec,
                           (ec_$eventcount_t *)&TIME_$CLOCKH,
                           &FIM_$QUIT_EC[asid] }},
        (ec_$wait_vals_t){{ sock_target,
                            (int32_t)(TIME_$CLOCKH + (uint32_t)(uint16_t)*timeout),
                            quit_target }});

    if (result == 0) {
        /* Socket message arrived */
        *status_ret = status_$ok;
    } else if (result == 1) {
        /* Timeout */
        *status_ret = status_$msg_time_out;
    } else if (result == 2) {
        /* Quit signal - save quit value (00e59ca2 - 00e59cca) */
        FIM_$QUIT_VALUE[asid] = (uint32_t)FIM_$QUIT_EC[asid].value;
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
 *
 * TODO(source-4v0i): the original (0x00e59ba4) takes only two arguments -
 * it allocates the status locally (link.w A6,-0x8 / pea (-0x4,A6)) and
 * never stores it back to a caller-supplied location; the boolean result
 * is seq on that local.  The third parameter here is not in the binary.
 */
int8_t MSG_$WAIT(msg_$socket_t *socket, int16_t *timeout, status_$t *status_ret)
{
    status_$t status;

    MSG_$WAITI(socket, timeout, &status);
    *status_ret = status;
    return (status == status_$ok) ? -1 : 0;
}
