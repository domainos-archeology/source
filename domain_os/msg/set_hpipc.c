/*
 * MSG_$SET_HPIPC - Verify that the caller owns a socket
 *
 * Despite the name, the routine only validates: it reports
 * status_$ok when the calling address space owns the socket and never
 * touches its second argument.  HPIPC = High Performance IPC.
 *
 * Original address: 0x00E59140 (88 bytes)
 *
 * Assembly:
 *   00e59148  lea (0xe80d84).l,A5       ; MSG_$UNWIRED_DATA
 *   00e5914e  movea.l (0x8,A6),A0       ; socket
 *   00e59152  movea.l (0x10,A6),A1      ; status_ret - the THIRD argument,
 *                                       ; so 0x0C is read by nothing
 *   00e59156  move.w (A0),D0w / ble / cmpi.w #0xe0,D0w / ble
 *   00e59160  0x290001 socket out of range
 *   00e59168..00e5917e  the 1-based ownership bitmap test
 *   00e59184  0x290005 no owner
 *   00e5918c  clr.l (A1)                ; status_$ok
 */

#include "msg/msg_internal.h"

void MSG_$SET_HPIPC(msg_$socket_t *socket, void *param2, status_$t *status_ret)
{
    int16_t sock_num;
    uint16_t asid;
    uint16_t byte_index;
    const uint8_t *bitmap;

    (void)param2;   /* 0x0C is never read by the original */

    sock_num = *socket;

    /* 0xE59156 */
    if (sock_num <= 0 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* 0xE59168 - 0xE5917E */
    asid = PROC1_$AS_ID;
    bitmap = MSG_$UNWIRED_DATA.ownership[sock_num];
    byte_index = (uint16_t)((0x3Fu - asid) >> 3);

    if ((bitmap[byte_index] & (1u << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    *status_ret = status_$ok;       /* 0xE5918C */
}
