/*
 * MSG_$FREE_ASID - Close every socket an address space still owns
 *
 * Called during address-space teardown.
 *
 * Original address: 0x00E74E2C (86 bytes)
 *
 * Assembly:
 *   00e74e34  movea.l (0x8,A6),A2       ; asid_ptr
 *   00e74e38  move.w #0xdf,D2w          ; dbf count -> 224 passes
 *   00e74e3c  movea.l #0xe80d84,A3
 *   00e74e42  moveq #0x1,D3             ; socket = 1
 *   00e74e44  addq.l #0x8,A3            ; A3 = base + 8, i.e. &owners[1]
 *   00e74e48  movea.l A3,A0             ; loop top
 *   00e74e4a  moveq #0x3f,D0 / move.w (A2),D1w / sub.w D1w,D0w / lsr.w #0x3
 *   00e74e52  lea (0x1d8,A0),A1
 *   00e74e56  btst.b D1,(0x0,A1,D0w*0x1) / beq
 *   00e74e5c  move.w D3w,(-0x6,A6) / pea (-0x4,A6) / pea (-0x6,A6)
 *   00e74e68  jsr MSG_$CLOSEI
 *   00e74e70  addq.w #0x1,D3w / addq.l #0x8,A3 / dbf
 *
 * The loop therefore covers sockets 1..0xE0 INCLUSIVE, and the ASID is
 * re-read from the caller's word on every pass.
 */

#include "msg/msg_internal.h"

void MSG_$FREE_ASID(uint16_t *asid_ptr)
{
    int16_t sock_num;
    uint16_t asid;
    uint16_t byte_index;
    const uint8_t *bitmap;
    msg_$socket_t socket;
    status_$t status;

    for (sock_num = 1; sock_num <= MSG_MAX_SOCKET; sock_num++) {
        bitmap = MSG_$SOCK_OWNERS[sock_num];

        asid = *asid_ptr;                       /* 0xE74E4C, re-read */
        byte_index = (uint16_t)((0x3Fu - asid) >> 3);

        if ((bitmap[byte_index] & (1u << (asid & 7))) != 0) {
            socket = sock_num;
            MSG_$CLOSEI(&socket, &status);      /* 0xE74E68 */
        }
    }
}
