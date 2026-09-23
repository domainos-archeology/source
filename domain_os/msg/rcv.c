/*
 * MSG_$RCV, MSG_$RCVI - Receive a message
 *
 * MSG_$RCVI validates the socket number and the caller's ownership of it,
 * then hands sixteen of its arguments to MSG_$$RCV_INTERNAL
 * (msg/rcv_internal.c) together with two locals that catch the netbuf
 * event-count words.
 *
 * MSG_$RCV is the short form: it supplies throwaway locals for the four
 * destination-address parameters and for the msg_$hw_addr_t record, of
 * which only the first word (proto_family) is handed back.
 *
 * Original addresses:
 *   MSG_$RCV:  0x00E594F4 (84 bytes)
 *   MSG_$RCVI: 0x00E596B2 (164 bytes)
 * Both in the map's MSG_UNWIRED object at 0xE5911C (size 0x100C).
 * Re-emitted from the disassembly 0x00E594F4-0x00E59546 and
 * 0x00E596B2-0x00E59754.
 */

#include "msg/msg_internal.h"

/*
 * MSG_$RCVI - Receive a message on a socket the caller owns.
 *
 * Sixteen arguments at (0x8,A6)..(0x44,A6).
 *
 *   0x00E596BA  lea (0xe80d84).l,A5       ; MSG_$DATA
 *   0x00E596C0  movea.l (0x8,A6),A2       ; socket
 *   0x00E596C4  movea.l (0x44,A6),A3      ; status_ret
 *   0x00E596C8  move.w (A2),D0w / ble / cmpi.w #0xe0,D0w / ble
 *   0x00E596D2  0x290001 socket out of range
 *   0x00E596DA  lsl.w #0x3,D0w / lea (0x1d8,A0),A0    ; the 1-based bitmap
 *   0x00E596E0  moveq #0x3f,D0 / sub.w PROC1_$AS_ID / lsr.w #0x3
 *   0x00E596F0  btst.b D1,(0x0,A0,D0w*0x1) / bne
 *   0x00E596F6  0x290005 no owner
 *   0x00E596FE  subq.l #0x2,SP            ; unread Pascal result slot
 *   0x00E59700..0x00E59746  eighteen arguments, right to left
 *   0x00E59748  bsr MSG_$$RCV_INTERNAL
 *
 * The two words at A6-0x38 and A6-0x36 are locals: MSG_$RCVI never reads
 * them back, so the netbuf event-count words are discarded here.
 */
void MSG_$RCVI(msg_$socket_t *socket,
               uint32_t *dest_net,
               uint32_t *dest_node,
               uint16_t *dest_sock,
               uint32_t *src_net,
               uint32_t *src_node,
               uint16_t *src_sock,
               msg_$hw_addr_t *hw_addr,
               uint16_t *msg_type,
               void *template,
               uint16_t *template_max,
               uint16_t *template_len_ret,
               void *data,
               uint16_t *data_max,
               uint16_t *data_len_ret,
               status_$t *status_ret)
{
    int16_t        sock_num;        /* D0w                              */
    uint16_t       asid;            /* D1w                              */
    uint16_t       byte_index;      /* D0w                              */
    const uint8_t *bitmap;          /* A0 (base + socket*8 + 0x1D8)     */
    uint16_t       ec_param1;       /* (-0x38,A6), never read back      */
    uint16_t       ec_param2;       /* (-0x36,A6), never read back      */

    /* 0x00E596C8-0x00E596D8: 1 <= socket <= 0xE0, both bounds signed. */
    sock_num = (int16_t)*socket;
    if (sock_num <= 0 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;         /* 0x290001 */
        return;
    }

    /* 0x00E596DA-0x00E596F4: the ownership bitmap is the 1-based table at
     * MSG_$DATA + 0x1D8 + socket*8; the byte index is (0x3F - asid) >> 3
     * in word arithmetic with a logical shift, and btst.b numbers bits
     * mod 8. */
    asid       = PROC1_$AS_ID;
    bitmap     = MSG_$SOCK_OWNERS[sock_num];
    byte_index = (uint16_t)((uint16_t)(0x3F - asid) >> 3);
    if ((bitmap[byte_index] & (uint8_t)(1 << (asid & 7))) == 0) {
        /* 0x00E596F6-0x00E596FC */
        *status_ret = status_$msg_no_owner;                    /* 0x290005 */
        return;
    }

    /* 0x00E596FE-0x00E59748: the two "max" words are dereferenced here
     * (0x00E59712 / 0x00E59720 `move.w (An),-(SP)`), the socket word too
     * (0x00E59746). */
    MSG_$$RCV_INTERNAL((uint16_t)sock_num,
                       dest_net, dest_node, dest_sock,
                       src_net, src_node, src_sock,
                       hw_addr, msg_type,
                       template, *template_max, template_len_ret,
                       data, *data_max, data_len_ret,
                       &ec_param1, &ec_param2,
                       status_ret);

    /* 0x00E5974C-0x00E59754 */
}

/*
 * MSG_$RCV - the short receive form.
 *
 * Twelve arguments at (0x8,A6)..(0x34,A6).
 *
 *   0x00E594F4  link.w A6,-0x30
 *   0x00E594F8..0x00E59534  sixteen pushes, right to left:
 *     (0x34) status, (0x30) data_len_ret, (0x2c) data_max, (0x28) data,
 *     (0x24) template_len_ret, (0x20) template_max, (0x1c) template,
 *     (0x18) msg_type, pea (-0x20,A6) hw_addr local, (0x10) src_sock,
 *     (0xc) src_node, pea (-0x24,A6) src_net, pea (-0x2e,A6) dest_sock,
 *     pea (-0x28,A6) dest_node, pea (-0x2c,A6) dest_net, (0x8) socket
 *   0x00E59538  bsr MSG_$RCVI
 *   0x00E5953C  movea.l (0x14,A6),A0 / move.w (-0x20,A6),(A0)
 *
 * The last two instructions copy hw_addr.proto_family - the FIRST word of
 * the record - into the caller's fourth argument.  Everything else the
 * record carries is thrown away.
 */
void MSG_$RCV(msg_$socket_t *socket,
              uint32_t *src_node,
              uint16_t *src_sock,
              uint16_t *proto_family_ret,
              uint16_t *msg_type,
              void *template,
              uint16_t *template_max,
              uint16_t *template_len_ret,
              void *data,
              uint16_t *data_max,
              uint16_t *data_len_ret,
              status_$t *status_ret)
{
    uint32_t       dest_net;        /* (-0x2C,A6) */
    uint32_t       dest_node;       /* (-0x28,A6) */
    uint16_t       dest_sock;       /* (-0x2E,A6) */
    uint32_t       src_net;         /* (-0x24,A6) */
    msg_$hw_addr_t hw_addr;         /* (-0x20,A6) */

    /* 0x00E594F8-0x00E59538 */
    MSG_$RCVI(socket,
              &dest_net, &dest_node, &dest_sock,
              &src_net, src_node, src_sock,
              &hw_addr, msg_type,
              template, template_max, template_len_ret,
              data, data_max, data_len_ret,
              status_ret);

    /* 0x00E5953C-0x00E59540: only the record's first word survives. */
    *proto_family_ret = hw_addr.proto_family;
}
