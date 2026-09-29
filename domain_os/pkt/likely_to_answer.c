/*
 * PKT_$LIKELY_TO_ANSWER - Is this node likely to respond?
 *
 * Asks RIP for the next hop toward a node.  If the route is direct and the
 * outgoing port is of type 4, the node is probed with a ping on socket 0x0D
 * (up to three sends, each waited on until the header's timeout expires);
 * otherwise the answer comes from the "recently missing" list alone.
 *
 * Either way the node's entry in the visibility table is updated before
 * returning, and a node that did not answer leaves
 * status_$network_remote_node_failed_to_respond behind.
 *
 * Original address: 0x00E1299E (534 bytes)
 * Module base: A5 = 0x00E24C9C = PKT_$DATA (0x00E129A6 "lea (0xe24c9c).l,A5")
 */

#include "pkt/pkt_internal.h"

boolean PKT_$LIKELY_TO_ANSWER(void *addr_info, status_$t *status_ret)
{
    const pkt_$net_addr_t *addr = (const pkt_$net_addr_t *)addr_info;

    /*
     * A6-0x20: the rip_$dest_addr_t handed to RIP_$FIND_NEXTHOP.  The original
     * never clears it - only .network and the low 20 bits of .host_lo are
     * written, so .host_hi and the top 12 bits of .host_lo are whatever was on
     * the stack.  RIP_$FIND_NEXTHOP copies all ten bytes into the caller's
     * nexthop buffer but every consumer masks the node id to 20 bits, so the
     * stale bits are harmless.  Reproduced here deliberately.
     */
    rip_$dest_addr_t dest;
    rip_$nexthop_t nexthop;         /* A6-0x10, "pea (-0x10,A6)" */
    int16_t port;                   /* A6-0x72 */
    int16_t route_result;
    boolean result;                 /* D2 */
    boolean need_ping;              /* D0.b at 0x00E12A18 */
    boolean got_response;           /* D6 */
    uint16_t sock_num;              /* A6-0x70, then D4 */
    int16_t request_id;             /* D3 */
    int16_t retry;                  /* D5, the dbf counter */
    sock_$sock_t    **ec_slot;      /* A6-0x88 */
    int32_t wait_val;               /* D2 once the socket is open */
    int32_t deadline;               /* A6-0x5C */
    uint16_t retry_hint;            /* A6-0x68 */
    uint16_t resp_timeout;          /* A6-0x66 */
    app_$receive_rec_t recv;        /* A6-0x50 (the record APP_$RECEIVE fills) */
    pkt_$internet_hdr_t *req_hdr;   /* A6-0x50 +0x00, "movea.l (-0x50,A6),A0" */
    uint16_t data_len;              /* A6-0x6E */
    int16_t resp_id;                /* D7 */
    uint32_t hdr_ppn;               /* A6-0x54 */

    /*
     * 0x00E129B4  move.l (A0),(-0x20,A6)
     * 0x00E129B8  andi.l #-0x100000,(-0x1a,A6)      A6-0x1A == &dest.host_lo
     * 0x00E129C0  move.l (0x4,A0),D0
     * 0x00E129C4  or.l D0,(-0x1a,A6)
     */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
    dest.network = addr->network;
    dest.host_lo = (dest.host_lo & 0xFFF00000u) | addr->node;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    /*
     * 0x00E129C8 - 0x00E129E0: a word result slot then, right to left,
     * status_ret, &nexthop, &port, the word 0 flag, &dest.
     */
    route_result = RIP_$FIND_NEXTHOP(&dest, false, &port, &nexthop, status_ret);

    /* 0x00E129E4: the port is loaded before the status is tested. */

    if (*status_ret != status_$ok) {
        return false;               /* 0x00E129EC "clr.b D2b" */
    }

    result = true;                  /* 0x00E129F2 "st D2b" */

    /*
     * Ping only a direct route out of a type-4 port.
     * 0x00E129F4  tst.w D0w / bne -> no ping
     * 0x00E12A08  movea.l (A1),A3       A3 = ROUTE_$PORTP[port]
     * 0x00E12A0A  cmpi.w #0x4,(0x2e,A3) / bne -> no ping
     * 0x00E12A12  move.b D2b,D0b        need_ping = result (true)
     * 0x00E12A16  clr.b D0b
     * 0x00E12A18  bmi.b -> ping path
     */
    need_ping = false;
    if (route_result == 0) {
        if (ROUTE_$WIRED_DATA.portp[port]->port_type == 4) {
            need_ping = result;
        }
    }

    if (need_ping >= 0) {
        /*
         * No probe: the answer is the complement of the recently-missing flag.
         * 0x00E12A1E  move.l (0x4,A0),-(SP)   the raw node, not the masked one
         * 0x00E12A22  bsr.w PKT_$RECENTLY_MISSING
         * 0x00E12A26  not.b D0b
         * 0x00E12A2A  bra.w 0x00E12BA2 - the failure status is set
         *             unconditionally on this path.
         */
        result = (boolean)~PKT_$RECENTLY_MISSING(addr->node);
        *status_ret = status_$network_remote_node_failed_to_respond;
        return result;
    }

    /*
     * 0x00E12A2E - 0x00E12A42: SOCK_$ALLOCATE(&sock_num, 0x20001, 0x400).
     * A negative (true) return means the socket was allocated.
     */
    if (SOCK_$ALLOCATE(&sock_num, 0x20001, PKT_CHUNK_SIZE) >= 0) {
        *status_ret = status_$network_no_more_free_sockets;
        /* 0x00E12A54 branches to the common exit, which returns D2 - still
         * the "true" stored at 0x00E129F2. */
        return result;
    }

    got_response = false;           /* 0x00E12A58 "clr.b D6b" */
    request_id = PKT_$NEXT_ID();    /* 0x00E12A5A */

    /*
     * 0x00E12A64  movea.l #0xe28db4,A3
     * 0x00E12A6A  lsl.l #0x2,D5                D5 = sock_num * 4
     * 0x00E12A6C  lea (0x0,A3,D5*0x1),A0
     * 0x00E12A70  move.l A0,(-0x88,A6)         the slot address is cached ...
     * 0x00E12A74  movea.l (-0x4,A0),A1         ... but re-dereferenced on
     * 0x00E12A78  move.l (A1),D2               every EC_$WAIT (0x00E12B54).
     */
    ec_slot = &SOCK_$DATA.socket_ptr[sock_num];
    wait_val = (*ec_slot)->ec.value + 1;

    retry = 2;                      /* 0x00E12A7A "moveq #0x2,D5" */

    for (;;) {
        /*
         * 0x00E12A84 - 0x00E12ABE: fifteen arguments plus the word result
         * slot; 0x00E12AC2 pops 0x34 bytes.  Pushed right to left, so the
         * declaration order below is the reverse of the instruction order:
         *   0x00E12ABC  move.l (A0),-(SP)          addr->network
         *   0x00E12AB8  move.l (0x4,A0),-(SP)      addr->node, UNMASKED
         *   0x00E12AB0  move.w #0xd,-(SP)          dest_sock
         *   0x00E12AAC  pea (-0x1).w               src_node_or = -1
         *   0x00E12AA6  move.l (0x00e245a4).l,-(SP) NODE_$ME
         *   0x00E12AA4  move.w D4w,-(SP)           src_sock = our socket
         *   0x00E12AA0  pea (0x68,A5)              &PKT_$DATA.ping_template
         *   0x00E12A9E  move.w D3w,-(SP)           request_id
         *   0x00E12A9A  pea (0x5a,A5)              &PKT_$DATA.ping_req_hdr
         *   0x00E12A96  move.w #0x2,-(SP)          template_len = 2
         *   0x00E12A92  pea (0x120,PC)             &pkt_$no_data
         *   0x00E12A90  clr.w -(SP)                data_len = 0
         *   0x00E12A8C  pea (-0x68,A6)             &retry_hint
         *   0x00E12A88  pea (-0x66,A6)             &resp_timeout
         *   0x00E12A86  pea (A2)                   status_ret
         */
        PKT_$SEND_INTERNET(addr->network, addr->node, PKT_PING_SOCKET,
                           -1, NODE_$ME, sock_num,
                           &PKT_$DATA.ping_template, (uint16_t)request_id,
                           &PKT_$DATA.ping_req_hdr, 2,
                           (void *)&pkt_$no_data, 0,
                           &retry_hint, &resp_timeout,
                           status_ret);

        /* 0x00E12AC6: the timeout word is read before the status is tested. */

        if (*status_ret != status_$ok) {
            goto cleanup;           /* 0x00E12ACC "bne.w 0x00E12B7A" */
        }

        /*
         * 0x00E12AD0  andi.l #0xffff,D0     zero-extend the timeout word
         * 0x00E12AD6  add.l (0x00e2b0d4).l,D0
         * 0x00E12ADC  addq.l #0x1,D0
         */
        deadline = (int32_t)(TIME_$CLOCKH + (uint32_t)resp_timeout + 1);

        /* 0x00E12AE2 enters the loop at the EC_$WAIT, not at the receive. */
        goto ec_wait;

    receive:
        wait_val++;                 /* 0x00E12AE4 "addq.l #0x1,D2" */

        /* 0x00E12AE6 - 0x00E12AF6: result slot, status_ret, &recv, sock_num */
        APP_$RECEIVE(sock_num, &recv, status_ret);

        if (*status_ret != status_$ok) {
            goto check_response;    /* 0x00E12AFC "bne.b 0x00E12B72" */
        }

        /*
         * 0x00E12AFE "movea.l (-0x50,A6),A0" - the reply record APP_$RECEIVE
         * left at +0x00 is what PKT parses as the response header.
         */
        req_hdr = (pkt_$internet_hdr_t *)ARCH_VA_TO_PTR(recv.reply);
        data_len = req_hdr->data_len;        /* 0x00E12B02 "(0x4,A0)" */
        resp_id = req_hdr->request_id;       /* 0x00E12B0C "(0x6,A0)" */

        /*
         * 0x00E12B08  move.l (-0x4c,A6),D0      recv.data
         * 0x00E12B10  andi.w #-0x400,D0w        word AND: only bits 0..9 die
         * 0x00E12B14  move.l D0,(-0x54,A6)      into a separate local
         */
        hdr_ppn = recv.data & 0xFFFFFC00u;
        NETBUF_$RTN_HDR(&hdr_ppn);           /* 0x00E12B1C */

        /* 0x00E12B24 "tst.l (-0x48,A6)" - the first data buffer slot */
        if (recv.data_pages[0] != 0) {
            PKT_$DUMP_DATA(recv.data_pages, (int16_t)data_len);  /* 0x00E12B34 */
        }

        /* 0x00E12B3A "cmp.w D7w,D3w" - our request id against the reply's */
        if (request_id == resp_id) {
            got_response = true;    /* 0x00E12B3E "st D6b" */
            goto cleanup;           /* 0x00E12B40 */
        }
        /* Wrong id: fall through and keep waiting. */

    ec_wait:
        /*
         * 0x00E12B42 - 0x00E12B64.  Two 3-element arrays by value (24 bytes);
         * pushed right to left, so the values go down before the pointers:
         *   0x00E12B42  pea (0x1).w                vals[2] = 1
         *   0x00E12B46  move.l (-0x5c,A6),-(SP)    vals[1] = deadline
         *   0x00E12B4A  move.l D2,-(SP)            vals[0] = wait_val
         *   0x00E12B4C  pea (A3)   A3 == 0         ecs[2] = NULL
         *   0x00E12B4E  move.l #0xe2b0d4,-(SP)     ecs[1] = &TIME_$CLOCKH
         *   0x00E12B5C  pea (A1)                   ecs[0] = *ec_slot
         * ecs[2] is NULL so vals[2] is never looked at; it is kept for
         * fidelity.  The socket's event count is re-read from the table on
         * every pass (0x00E12B54 / 0x00E12B58).
         *
         * 0x00E12B68  tst.w D0w / seq D7b / tst.b D7b / bmi -> receive
         */
        if (EC_$WAIT((ec_$wait_ecs_t){{ &(*ec_slot)->ec,
                                        (ec_$eventcount_t *)&TIME_$CLOCKH,
                                        NULL }},
                     (ec_$wait_vals_t){{ wait_val, deadline, 1 }}) == 0) {
            goto receive;
        }

    check_response:
        if (got_response < 0) {
            break;                  /* 0x00E12B74 "bmi.b 0x00E12B7A" */
        }
        if (retry-- == 0) {
            break;                  /* 0x00E12B76 "dbf D5w,0x00E12A84" */
        }
    }

cleanup:
    SOCK_$CLOSE(sock_num);                              /* 0x00E12B7E */
    PKT_$NOTE_VISIBLE(addr->node, got_response);        /* 0x00E12B92 */

    result = got_response;          /* 0x00E12B98 "move.b D6b,D2b" */

    /*
     * 0x00E12B9A  tst.l (A2) / bne -> return
     * 0x00E12B9E  tst.b D6b / bmi -> return
     * 0x00E12BA2  move.l #0x110007,(A2)
     */
    if (*status_ret == status_$ok && got_response >= 0) {
        *status_ret = status_$network_remote_node_failed_to_respond;
    }

    return result;                  /* 0x00E12BA8 "move.b D2b,D0b" */
}
