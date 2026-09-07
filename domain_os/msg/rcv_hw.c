/*
 * MSG_$RCV_HW - Receive a message, also reporting the netbuf event words
 *
 * The same wrapper as MSG_$RCV, except that the two words SOCK_$PUT stored
 * alongside the packet header are handed back instead of discarded, and the
 * caller supplies no destination-address buffers.
 *
 * Original address: 0x00E59950 (172 bytes)
 *
 * Assembly:
 *   00e59958  lea (0xe80d84).l,A5       ; MSG_$DATA
 *   00e5995e  movea.l (0x8,A6),A2       ; socket
 *   00e59962  movea.l (0x3c,A6),A3      ; status_ret
 *   00e59966  move.w (A2),D0w / ble / cmpi.w #0xe0,D0w / ble
 *   00e59970  0x290001 socket out of range   -> bra 0x00e599ea
 *   00e59978..00e5998e  the ownership bitmap test
 *   00e59994  0x290005 no owner              -> bra 0x00e599ea
 *   00e5999c  subq.l #0x2,SP            ; unread Pascal result slot
 *   00e5999e..00e599e4  eighteen arguments, right to left
 *   00e599e6  bsr MSG_$$RCV_INTERNAL
 *   00e599ea  movea.l (0x14,A6),A0 / move.w (-0x50,A6),(A0)
 *
 * ORIGINAL DEFECT, reproduced below: both error branches jump to 0x00E599EA,
 * so *proto_family_ret is written from the msg_$hw_addr_t local even when
 * MSG_$$RCV_INTERNAL never ran and the local is uninitialised.
 */

#include "msg/msg_internal.h"

void MSG_$RCV_HW(msg_$socket_t *socket,
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
                 uint16_t *ec_param1_ret,
                 uint16_t *ec_param2_ret,
                 status_$t *status_ret)
{
    int16_t sock_num;
    uint16_t asid;
    uint16_t byte_index;
    const uint8_t *bitmap;
    uint32_t dest_net;              /* A6-0x60 */
    uint32_t dest_node;             /* A6-0x5C */
    uint16_t dest_sock;             /* A6-0x62 */
    uint32_t src_net;               /* A6-0x58 */
    msg_$hw_addr_t hw_addr;         /* A6-0x50 */

    sock_num = *socket;

    /* 0xE59966 */
    if (sock_num <= 0 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        goto store_proto_family;
    }

    /* 0xE59978 - 0xE5998E */
    asid = PROC1_$AS_ID;
    bitmap = MSG_$SOCK_OWNERS[sock_num];
    byte_index = (uint16_t)((0x3Fu - asid) >> 3);

    if ((bitmap[byte_index] & (1u << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        goto store_proto_family;
    }

    /* 0xE599E6 */
    MSG_$$RCV_INTERNAL((uint16_t)sock_num,
                       &dest_net, &dest_node, &dest_sock,
                       &src_net, src_node, src_sock,
                       &hw_addr, msg_type,
                       template, *template_max, template_len_ret,
                       data, *data_max, data_len_ret,
                       ec_param1_ret, ec_param2_ret,
                       status_ret);

store_proto_family:
    /*
     * 0xE599EA is the common exit for all three paths, including the two
     * that never touched hw_addr.  Preserved verbatim.
     */
    *proto_family_ret = hw_addr.proto_family;
}
