/*
 * MSG_$SEND_HW - Send a message to an explicit network/socket pair
 *
 * The by-reference form of MSG_$$SEND that picks its port from the
 * network and socket carried in a msg_$hw_addr_t rather than letting the
 * header builder choose one.
 *
 * Original address: 0x00E59B14 (144 bytes)
 *
 * Assembly:
 *   00e59b1c  movea.l (0x8,A6),A2       ; hw_addr
 *   00e59b20  movea.l (0x40,A6),A3      ; status_ret
 *   00e59b24  move.w (0x8,A2),D0w       ; hw_addr->reserved1 - the socket
 *   00e59b28  subq.l #0x2,SP            ; Pascal result slot
 *   00e59b2a  ext.l D0 / move.l D0,-(SP); SIGN-extended to a longword
 *   00e59b2e  move.w (0x6,A2),-(SP)     ; hw_addr->proto_subtype - the network
 *   00e59b32  jsr ROUTE_$FIND_PORT
 *   00e59b3c  cmpi.w #-0x1,D2w / bne
 *   00e59b42  0x2b0003 unknown network port
 *   00e59b4a..00e59b92  fifteen arguments for MSG_$$SEND, right to left
 *   00e59b94  jsr MSG_$$SEND
 *
 * The two fields the port lookup uses sit at msg_$hw_addr_t +0x06 and +0x08,
 * i.e. proto_subtype and reserved1.  MSG_$$RCV_INTERNAL fills +0x06 from the
 * reply header and leaves +0x08 alone, so a caller round-tripping a received
 * record has to supply +0x08 itself.
 */

#include "msg/msg_internal.h"

void MSG_$SEND_HW(msg_$hw_addr_t *hw_addr,
                  uint32_t *routing_key,
                  uint32_t *dest_node,
                  uint16_t *dest_sock,
                  int32_t *src_node_or,
                  uint32_t *src_node,
                  uint16_t *src_sock,
                  void *pkt_info,
                  uint16_t *request_id,
                  void *template,
                  uint16_t *template_len,
                  void *data,
                  uint16_t *data_len,
                  net_io_$send_info_t *send_info,
                  status_$t *status_ret)
{
    int16_t port_num;

    /* 0xE59B24 - 0xE59B32: the socket word is SIGN extended */
    port_num = ROUTE_$FIND_PORT(hw_addr->proto_subtype,
                                (int32_t)(int16_t)hw_addr->reserved1);

    /* 0xE59B3C */
    if (port_num == -1) {
        *status_ret = status_$internet_unknown_network_port;   /* 0x2B0003 */
        return;
    }

    /*
     * 0xE59B4A - 0xE59B94.  Every scalar is dereferenced here; the packet
     * info record, the template, the payload and the send-info record are
     * passed on by value.
     */
    MSG_$$SEND(port_num,
               *routing_key,
               *dest_node,
               *dest_sock,
               *src_node_or,
               *src_node,
               *src_sock,
               (const pkt_$info_t *)pkt_info,
               *request_id,
               template,
               *template_len,
               data,
               *data_len,
               send_info,
               status_ret);
}
