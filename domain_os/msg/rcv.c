/*
 * MSG_$RCV, MSG_$RCVI - Receive a message
 *
 * MSG_$RCVI validates the socket number and the caller's ownership of it,
 * then hands sixteen of its arguments to MSG_$$RCV_INTERNAL (msg/rcv_internal.c)
 * together with two locals that catch the netbuf event-count words.
 *
 * MSG_$RCV is the short form: it supplies throwaway locals for the four
 * destination-address parameters and for the msg_$hw_addr_t record, of which
 * only the first word (proto_family) is handed back.
 *
 * Original addresses:
 *   MSG_$RCV:  0x00E594F4 (84 bytes)
 *   MSG_$RCVI: 0x00E596B2 (164 bytes)
 */

#include "msg/msg_internal.h"

/*
 * MSG_$RCVI - Receive a message on a socket the caller owns.
 *
 * Assembly:
 *   00e596ba  lea (0xe80d84).l,A5       ; MSG_$DATA
 *   00e596c0  movea.l (0x8,A6),A2       ; socket
 *   00e596c4  movea.l (0x44,A6),A3      ; status_ret
 *   00e596c8  move.w (A2),D0w / ble / cmpi.w #0xe0,D0w / ble
 *   00e596d2  0x290001 socket out of range
 *   00e596da  lsl.w #0x3,D0w / lea (0x1d8,A0),A0    ; the 1-based bitmap
 *   00e596e0  moveq #0x3f,D0 / sub.w PROC1_$AS_ID / lsr.w #0x3
 *   00e596f0  btst.b D1,(0x0,A0,D0w*0x1) / bne
 *   00e596f6  0x290005 no owner
 *   00e596fe  subq.l #0x2,SP            ; unread Pascal result slot
 *   00e59700..00e59746  eighteen arguments, right to left
 *   00e59748  bsr MSG_$$RCV_INTERNAL
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
    int16_t sock_num;
    uint16_t asid;
    uint16_t byte_index;
    const uint8_t *bitmap;
    uint16_t ec_param1;             /* A6-0x38, never read back */
    uint16_t ec_param2;             /* A6-0x36, never read back */

    sock_num = *socket;

    /* 0xE596C8: 1 <= socket <= 0xE0, both bounds signed */
    if (sock_num <= 0 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /*
     * 0xE596DA - 0xE596F0: the ownership bitmap is the 1-based table at
     * MSG_$DATA + 0x1D8 + socket*8; the byte index is (0x3F - asid) >> 3 in
     * word arithmetic with a logical shift, and btst.b numbers bits mod 8.
     */
    asid = PROC1_$AS_ID;
    bitmap = MSG_$SOCK_OWNERS[sock_num];
    byte_index = (uint16_t)((0x3Fu - asid) >> 3);

    if ((bitmap[byte_index] & (1u << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    /* 0xE59748: the two word arguments are dereferenced here, not below */
    MSG_$$RCV_INTERNAL((uint16_t)sock_num,
                       dest_net, dest_node, dest_sock,
                       src_net, src_node, src_sock,
                       hw_addr, msg_type,
                       template, *template_max, template_len_ret,
                       data, *data_max, data_len_ret,
                       &ec_param1, &ec_param2,
                       status_ret);
}

/*
 * MSG_$RCV - the short receive form.
 *
 * Assembly:
 *   00e594f4  link.w A6,-0x30
 *   00e59518  pea (-0x20,A6)            ; the msg_$hw_addr_t local
 *   00e59524  pea (-0x24,A6)            ; src_net   (discarded)
 *   00e59528  pea (-0x2e,A6)            ; dest_sock (discarded)
 *   00e5952c  pea (-0x28,A6)            ; dest_node (discarded)
 *   00e59530  pea (-0x2c,A6)            ; dest_net  (discarded)
 *   00e59538  bsr MSG_$RCVI
 *   00e5953c  movea.l (0x14,A6),A0 / move.w (-0x20,A6),(A0)
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
    uint32_t dest_net;              /* A6-0x2C */
    uint32_t dest_node;             /* A6-0x28 */
    uint16_t dest_sock;             /* A6-0x2E */
    uint32_t src_net;               /* A6-0x24 */
    msg_$hw_addr_t hw_addr;         /* A6-0x20 */

    MSG_$RCVI(socket,
              &dest_net, &dest_node, &dest_sock,
              &src_net, src_node, src_sock,
              &hw_addr, msg_type,
              template, template_max, template_len_ret,
              data, data_max, data_len_ret,
              status_ret);

    /* 0xE59540: only the record's first word survives */
    *proto_family_ret = hw_addr.proto_family;
}
