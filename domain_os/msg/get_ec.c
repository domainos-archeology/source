/*
 * MSG_$GET_EC - Get an EC2-registered event count for a socket
 *
 * Validates the socket number and the caller's ownership of it, then
 * registers the socket's event count for user-level waiting.
 *
 * Original address: 0x00E59CDA (120 bytes)
 *
 * Assembly:
 *   00e59ce2  lea (0xe80d84).l,A5       ; MSG_$DATA
 *   00e59ce8  movea.l (0x8,A6),A2       ; socket
 *   00e59cec  movea.l (0x10,A6),A3      ; status_ret
 *   00e59cf0  move.w (A2),D0w / ble / cmpi.w #0xe0,D0w / ble
 *   00e59cfa  0x290001 socket out of range
 *   00e59d02..00e59d18  the 1-based ownership bitmap test
 *   00e59d1e  0x290005 no owner
 *   00e59d26  pea (A3)
 *   00e59d28  move.w (A2),D1w / movea.l #0xe28db4,A0 / lsl.w #0x2,D1w
 *   00e59d32  lea (0x0,A0,D1w*0x1),A1 / movea.l (-0x4,A1),A4
 *                                       ; = SOCK_$EVENT_COUNTERS[socket - 1]
 *   00e59d3a  pea (A4) / jsr EC2_$REGISTER_EC1
 *   00e59d42  movea.l (0xc,A6),A1 / move.l A0,(A1)
 *
 * The result is only stored on the success path; both error branches jump
 * past 0x00E59D42 and leave *ec untouched.
 */

#include "msg/msg_internal.h"

void MSG_$GET_EC(msg_$socket_t *socket, uint32_t *ec, status_$t *status_ret)
{
    int16_t sock_num;
    uint16_t asid;
    uint16_t byte_index;
    const uint8_t *bitmap;
    ec_$eventcount_t *sock_ec;

    sock_num = *socket;

    /* 0xE59CF0 */
    if (sock_num <= 0 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* 0xE59D02 - 0xE59D18 */
    asid = PROC1_$AS_ID;
    bitmap = MSG_$SOCK_OWNERS[sock_num];
    byte_index = (uint16_t)((0x3Fu - asid) >> 3);

    if ((bitmap[byte_index] & (1u << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    /* 0xE59D36: the socket number is re-read from the caller's word */
    sock_ec = SOCK_$EVENT_COUNTERS[*socket - 1];

    /* 0xE59D3C: the handle comes back in A0 */
    *ec = ARCH_PTR_TO_VA(EC2_$REGISTER_EC1(sock_ec, status_ret));
}
