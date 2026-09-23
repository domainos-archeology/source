/*
 * MSG_$SAR, MSG_$SARI - Send And Receive
 *
 * Sends one message from a freshly allocated user socket and waits on that
 * socket for the reply whose message type matches the request id the send
 * used.  The socket is closed again before returning.
 *
 * Original addresses:
 *   MSG_$SAR:  0x00E59D52 (126 bytes)
 *   MSG_$SARI: 0x00E59DD4 (478 bytes)
 * Both in the map's MSG_UNWIRED object at 0xE5911C (size 0x100C).
 * Re-verified instruction by instruction against 0x00E59D52-0x00E59DCE and
 * 0x00E59DD4-0x00E59FB0 (the 18-argument frame, the MSG_$$SEND push order,
 * the three-way EC_$WAIT and its 0/1/2/other dispatch, the receive locals).
 */

#include "msg/msg_internal.h"
#include "fim/fim.h"
#include "time/time.h"

/*
 * MSG_$SARI - the by-reference send-and-receive body.
 *
 * Assembly:
 *   00e59ddc  movea.l (0x4c,A6),A3      ; status_ret
 *   00e59de0  move.l #0x10400,-(SP) / move.l #0x10001,-(SP) / pea (-0x64,A6)
 *   00e59df0  jsr SOCK_$ALLOCATE_USER   ; queue depth 1, 1 hdr page,
 *                                       ; 1 data page, max data 0x400
 *   00e59dfe  tst.b D0b / bmi
 *   00e59e02  0x290004 no more sockets  ; note: no SOCK_$CLOSE on this path
 *   00e59e0c  (-0x6c,A6) = SOCK_$EVENT_COUNTERS[sock - 1]
 *   00e59e22  jsr PKT_$NEXT_ID          ; D3 = the request id
 *   00e59e2e  move.l (A1),D2            ; the socket eventcount's value
 *   00e59e30  (-0x50,A6) = FIM_$QUIT_VALUE[AS_ID] + 1
 *   00e59e48..00e59e8a  MSG_$$SEND(port -1, ..., src_sock = the temp socket,
 *                                  src_node = NODE_$ME, src_node_or = -1,
 *                                  request_id = D3)
 *   00e59e94  *xmit_status_ret = send_info.xmit_status
 *   00e59e9c  (-0x54,A6) = TIME_$CLOCKH + sign-extended *timeout
 *   00e59eae  tst.l (A3) / bne -> close and return
 *   00e59ec0  loop: addq.l #0x1,D2 then EC_$WAIT on
 *                   { socket ec, &TIME_$CLOCKH, &FIM_$QUIT_EC[AS_ID] }
 *   00e59ef8  0 -> receive, 1 -> timeout, 2 -> quit, anything else -> loop
 *   00e59f08..00e59f54  MSG_$$RCV_INTERNAL on the temp socket
 *   00e59f60  a receive error or a mismatched message type loops again
 *   00e59f6e  0x110007 remote node failed to respond
 *   00e59f76  0x120010 process quit, after saving the quit eventcount value
 *   00e59f9e  jsr SOCK_$CLOSE(temp socket)
 *
 * The wait value D2 is incremented at the TOP of the loop, so the first
 * EC_$WAIT already asks for value+1.
 */
void MSG_$SARI(int16_t *timeout,
               uint32_t *routing_key,
               uint32_t *dest_node,
               uint16_t *dest_sock,
               void *pkt_info,
               void *send_template,
               uint16_t *send_template_len,
               void *send_data,
               uint16_t *send_data_len,
               uint16_t *xmit_status_ret,
               msg_$hw_addr_t *hw_addr,
               void *rcv_template,
               uint16_t *rcv_template_max,
               uint16_t *rcv_template_len_ret,
               void *rcv_data,
               uint16_t *rcv_data_max,
               uint16_t *rcv_data_len_ret,
               status_$t *status_ret)
{
    uint16_t sock_num;              /* A6-0x64, then D4 */
    ec_$eventcount_t *sock_ec;      /* A6-0x6C */
    uint16_t request_id;            /* D3 */
    int32_t wait_val;               /* D2 */
    int32_t quit_val;               /* A6-0x50 */
    int32_t deadline;               /* A6-0x54 */
    net_io_$send_info_t send_info;  /* A6-0x38 */
    uint32_t rcv_dest_net;          /* A6-0x48 */
    uint32_t rcv_dest_node;         /* A6-0x44 */
    uint16_t rcv_dest_sock;         /* A6-0x60 */
    uint32_t rcv_src_net;           /* A6-0x40 */
    uint32_t rcv_src_node;          /* A6-0x3C */
    uint16_t rcv_src_sock;          /* A6-0x5E */
    uint16_t rcv_msg_type;          /* A6-0x68 */
    uint16_t ec_param1;             /* A6-0x5C */
    uint16_t ec_param2;             /* A6-0x5A */
    int16_t wait_result;

    /* 0xE59DE0: queue depth 1, one header page, one data page, 0x400 bytes */
    if (SOCK_$ALLOCATE_USER(&sock_num, 1, 1, 1, 0x400) >= 0) {
        *status_ret = status_$msg_no_more_sockets;      /* 0x290004 */
        return;
    }

    sock_ec = SOCK_$EVENT_COUNTERS[sock_num - 1];       /* 0xE59E0C */
    request_id = (uint16_t)PKT_$NEXT_ID();              /* 0xE59E22 */
    wait_val = (int32_t)sock_ec->value;                 /* 0xE59E2E */
    quit_val = (int32_t)FIM_$QUIT_VALUE[PROC1_$AS_ID] + 1;   /* 0xE59E30 */

    /* 0xE59E48 */
    MSG_$$SEND(-1,
               *routing_key,
               *dest_node,
               *dest_sock,
               -1,                          /* src_node_or, 0xE59E70 */
               NODE_$ME,                    /* 0xE59E6A */
               sock_num,                    /* src_sock, 0xE59E68 */
               (const pkt_$info_t *)pkt_info,
               request_id,
               send_template,
               *send_template_len,
               send_data,
               *send_data_len,
               &send_info,
               status_ret);

    /* 0xE59E94: the second word of the record */
    *xmit_status_ret = send_info.xmit_status;

    /* 0xE59E9C: the timeout word is SIGN extended before the add */
    deadline = (int32_t)(TIME_$CLOCKH + (uint32_t)(int32_t)*timeout);

    /* 0xE59EAE */
    if (*status_ret == status_$ok) {
        for (;;) {
            wait_val++;                                 /* 0xE59EC0 */

            /* 0xE59EEC */
            wait_result = EC_$WAIT(
                (ec_$wait_ecs_t){{ sock_ec,
                                   (ec_$eventcount_t *)&TIME_$CLOCKH,
                                   &FIM_$QUIT_EC[PROC1_$AS_ID] }},
                (ec_$wait_vals_t){{ wait_val, deadline, quit_val }});

            if (wait_result == 1) {
                /* 0xE59F6E */
                *status_ret = status_$network_remote_node_failed_to_respond;
                break;
            }

            if (wait_result == 2) {
                /* 0xE59F76 */
                *status_ret = status_$fault_process_quit;
                FIM_$QUIT_VALUE[PROC1_$AS_ID] =
                    (uint32_t)FIM_$QUIT_EC[PROC1_$AS_ID].value;
                break;
            }

            /* 0xE59EF8: anything but 0 that is not 1 or 2 waits again */
            if (wait_result != 0) {
                continue;
            }

            /* 0xE59F08 */
            MSG_$$RCV_INTERNAL(sock_num,
                               &rcv_dest_net, &rcv_dest_node, &rcv_dest_sock,
                               &rcv_src_net, &rcv_src_node, &rcv_src_sock,
                               hw_addr, &rcv_msg_type,
                               rcv_template, *rcv_template_max,
                               rcv_template_len_ret,
                               rcv_data, *rcv_data_max, rcv_data_len_ret,
                               &ec_param1, &ec_param2,
                               status_ret);

            /*
             * 0xE59F60 / 0xE59F66: a failed receive, or a reply carrying a
             * different message type, sends us back round the wait.
             */
            if (*status_ret != status_$ok) {
                continue;
            }
            if (rcv_msg_type != request_id) {
                continue;
            }
            break;
        }
    }

    /* 0xE59F9E */
    SOCK_$CLOSE(sock_num);
}

/*
 * MSG_$SAR - the short form.
 *
 * Assembly:
 *   00e59d58  lea (0xe80d84).l,A5
 *   00e59d5e  lea (A5),A0 / lea (-0x40,A6),A1 / moveq #0x6 / move.l (A0)+,(A1)+
 *   00e59d6c  move.w (A0)+,(A1)+        ; 7 longwords + 1 word = 30 bytes
 *   00e59d6e  movea.l (0x14,A6),A0 / move.w (A0),(-0x40,A6)
 *   00e59db6  pea (0x18,PC)             ; &MSG_$SAR_TIMEOUT (0x00E59DD0)
 *   00e59dbe  bsr MSG_$SARI
 *   00e59dc0  movea.l (0x2c,A6),A0 / move.w (-0x20,A6),(A0)
 *
 * Exactly MSG_$SEND's packet-info idiom: the 30-byte template is copied out
 * of msg_$data_t and the caller's flags word overwrites its first word.  Of
 * the msg_$hw_addr_t the receive fills in, only proto_family is handed back.
 */
void MSG_$SAR(int16_t *timeout,
              uint32_t *dest_node,
              uint16_t *dest_sock,
              uint16_t *flags,
              void *send_template,
              uint16_t *send_template_len,
              void *send_data,
              uint16_t *send_data_len,
              uint16_t *xmit_status_ret,
              uint16_t *proto_family_ret,
              void *rcv_template,
              uint16_t *rcv_template_max,
              uint16_t *rcv_template_len_ret,
              void *rcv_data,
              uint16_t *rcv_data_max,
              uint16_t *rcv_data_len_ret,
              status_$t *status_ret)
{
    pkt_$info_t pkt_info;           /* A6-0x40: only 30 of its 32 bytes */
    msg_$hw_addr_t hw_addr;         /* A6-0x20 */
    int i;

    /* 0xE59D5E: 7 longwords then a word out of msg_$data_t.send_template
     * (0x1E bytes; the record's last word is never copied and is left
     * whatever the stack held). */
    for (i = 0; i < 0x1E; i++) {
        ((uint8_t *)&pkt_info)[i] = MSG_$DATA->send_template[i];
    }

    /* 0xE59D6E-0xE59D72: the caller's flags word overwrites the template's
     * first word - the same idiom as MSG_$SEND (0x00E59A50). */
    pkt_info.flags = *flags;

    MSG_$SARI(timeout,
              (uint32_t *)&MSG_$SAR_TIMEOUT,    /* 0xE59DB6 */
              dest_node, dest_sock,
              &pkt_info,
              send_template, send_template_len,
              send_data, send_data_len,
              xmit_status_ret,
              &hw_addr,
              rcv_template, rcv_template_max, rcv_template_len_ret,
              rcv_data, rcv_data_max, rcv_data_len_ret,
              status_ret);

    /* 0xE59DC4: only the record's first word survives */
    *proto_family_ret = hw_addr.proto_family;
}
