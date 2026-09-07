/*
 * MSG_$TEST_FOR_MESSAGE - Test whether a message is waiting on a socket
 *
 * A non-blocking check: it reports the socket's event count value and returns
 * a Domain boolean saying whether the socket's receive queue is non-empty.
 *
 * Original address: 0x00E59FB2 (122 bytes)
 */

#include "msg/msg_internal.h"

boolean MSG_$TEST_FOR_MESSAGE(msg_$socket_t *socket, uint32_t *ec_value,
                              status_$t *status_ret)
{
    int16_t sock_num;       /* D0w */
    int16_t asid;           /* D1w */
    int16_t byte_index;     /* D0w */
    uint8_t *bitmap;        /* A2 */
    sock_$sock_t *sock;     /* A0 */

    /*
     * 0x00E59FC8  move.w (A0),D0w / ble / cmpi.w #0xe0,D0w / ble
     * Socket numbers run 1..0xE0 inclusive.
     */
    sock_num = (int16_t)*socket;
    if (sock_num <= 0 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;   /* 0x00E59FD2 */
        /*
         * 0x00E59FD8 jumps to the common exit with D0w still holding the
         * socket number, so the byte the caller reads back is its low byte.
         */
        return (boolean)(int8_t)sock_num;
    }

    /*
     * 0x00E59FDA  lsl.w #0x3,D0w / lea (0x0,A5,D0w*0x1),A2 / lea (0x1d8,A2),A2
     * i.e. the one-based ownership table.
     */
    bitmap = MSG_$SOCK_OWNERS[sock_num];

    /*
     * 0x00E59FE0  moveq #0x3f,D0 / move.w PROC1_$AS_ID,D1w / sub.w D1w,D0w /
     *             lsr.w #0x3,D0w / btst.b D1,(0x0,A2,D0w*0x1)
     * All of it is word arithmetic with a LOGICAL shift, and the btst
     * numbers bits modulo 8.
     */
    asid = (int16_t)PROC1_$AS_ID;
    byte_index = (int16_t)((uint16_t)(0x3F - asid) >> 3);

    if ((bitmap[byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;             /* 0x00E59FF6 */
        /*
         * 0x00E59FFC reaches the exit with D0w holding the byte index, so
         * that is what the caller sees.
         */
        return (boolean)(int8_t)byte_index;
    }

    /*
     * 0x00E59FFE  move.w (A0),D1w / movea.l #0xe28db4,A2 / lsl.w #0x2,D1w /
     *             lea (0x0,A2,D1w*0x1),A2 / movea.l (-0x4,A2),A0
     * The -4 makes this SOCK_$EVENT_COUNTERS[socket - 1].
     */
    sock = (sock_$sock_t *)SOCK_$EVENT_COUNTERS[sock_num - 1];

    *ec_value = (uint32_t)sock->ec.value;               /* 0x00E5A014 */
    *status_ret = status_$ok;                           /* 0x00E5A016 */

    /*
     * 0x00E5A018  clr.w D0w / move.b (0x15,A0),D0b / tst.w D0w / sne D0b
     * A Domain boolean: 0xFF when anything is queued.
     */
    return sock->queue_count != 0 ? true : false;
}
