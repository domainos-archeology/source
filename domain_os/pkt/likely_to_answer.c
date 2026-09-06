/*
 * PKT_$LIKELY_TO_ANSWER - Check if node is likely to respond
 *
 * Determines if a node is likely to respond to requests. May send
 * a ping packet to verify the node is reachable.
 *
 * The algorithm:
 * 1. Do a route lookup to find next hop
 * 2. If direct route (sVar2 == 0) and port type is 4, need to ping
 * 3. If indirect route or other port type, check recently missing list
 * 4. For ping: allocate socket, send ping, wait for response
 * 5. Update visibility based on response
 *
 * Original address: 0x00E1299E
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

/*
 * Ping request template - 2 bytes of request data
 * The template at 0xE24CF6 contains the ping request format
 */
static uint8_t ping_template[2] = { 0, 0 };

/*
 * Ping request info structure at 0xE24D04
 * This configures the ping request behavior
 */
static pkt_$request_template_t ping_request_info = {
    .type = 2,          /* Request type */
    .length = 0,        /* Data length */
    .id = 0,            /* Request ID (filled in) */
    .flags = 0,         /* Flags */
    .protocol = 0,      /* Protocol */
    .retry_count = 0,   /* Retry count */
    .pad_0a = 0,        /* Padding */
    .field_0c = 0       /* Protocol field */
};

int8_t PKT_$LIKELY_TO_ANSWER(void *addr_info, status_$t *status_ret)
{
    uint32_t *addr = (uint32_t *)addr_info;
    uint32_t routing_key;
    uint32_t dest_node;
    int16_t route_result;
    int16_t port;
    int8_t result;
    int16_t need_ping;
    uint8_t nexthop[6];
    uint16_t sock_num;
    int16_t request_id;
    int16_t retry_count;
    ec_$eventcount_t *sock_ec;
    int32_t wait_val;
    int32_t timeout_val;
    uint16_t len_out[5];
    void *recv_pkt;
    uint16_t recv_len;
    int16_t recv_id;
    uint32_t recv_ppn;
    uint32_t data_buffers[10];

    /* Extract routing key and destination node from address info */
    routing_key = addr[0];
    dest_node = addr[1] & 0x000FFFFF;  /* Node ID is low 20 bits */

    /* Look up route to destination */
    route_result = RIP_$FIND_NEXTHOP(addr_info, 0, &port, nexthop, status_ret);

    if (*status_ret != status_$ok) {
        return 0;
    }

    /*
     * Determine if we need to ping.
     * If direct route (route_result == 0) and port type is 4,
     * we need to verify the node is reachable.
     */
    need_ping = 0;
    if (route_result == 0) {
        /* Get port info and check type */
        /* Port type 4 indicates we should ping */
        /* TODO(source-j33): Access ROUTE_$PORTP[port] to get port info and check type at offset 0x2E */
        need_ping = 1;  /* Simplified: assume ping needed for direct routes */
    }

    if (!need_ping) {
        /* No ping needed - just check recently missing list */
        result = PKT_$RECENTLY_MISSING(dest_node);
        result = ~result;  /* Invert: if recently missing, not likely to answer */
        *status_ret = status_$network_remote_node_failed_to_respond;
        return result;
    }

    /* Need to send a ping to verify reachability */

    /* Allocate a socket for the ping */
    if (SOCK_$ALLOCATE(&sock_num, 0x20001, PKT_CHUNK_SIZE) >= 0) {
        *status_ret = status_$network_no_more_free_sockets;
        return (int8_t)0xFF;
    }

    result = 0;
    request_id = PKT_$NEXT_ID();
    retry_count = 2;

    /*
     * Get the socket's event count for waiting
     * 00e12a64  movea.l #0xe28db4,A3
     * 00e12a6a  lsl.l #0x2,D5            ; D5 = sock_num * 4
     * 00e12a6c  lea (0x0,A3,D5*0x1),A0
     * 00e12a74  movea.l (-0x4,A0),A1
     * 00e12a78  move.l (A1),D2
     * 00e12a7c  addq.l #0x1,D2
     * i.e. *(0xe28db0 + sock_num*4) == SOCK_$EVENT_COUNTERS[sock_num - 1].
     */
    sock_ec = SOCK_$EVENT_COUNTERS[sock_num - 1];
    wait_val = sock_ec->value + 1;

    while (retry_count >= 0) {
        /*
         * Send ping request
         * TODO(source-hny4): this argument list does not match the 14
         * longwords/words pushed at 00e12a84 - 00e12abe.
         */
        PKT_$SEND_INTERNET(routing_key, dest_node, PKT_PING_SOCKET,
                           (int32_t)-1, NODE_$ME, sock_num,
                           &ping_request_info, request_id,
                           ping_template, 2,
                           NULL, 0,
                           len_out, NULL, status_ret);

        /* 00e12aca tst.l (A2) / bne - the send reports through status_ret */
        if (*status_ret != status_$ok) {
            break;
        }

        /*
         * Calculate timeout
         * 00e12ad0  andi.l #0xffff,D0
         * 00e12ad6  add.l (0x00e2b0d4).l,D0
         * 00e12adc  addq.l #0x1,D0
         */
        timeout_val = (int32_t)(TIME_$CLOCKH + (uint32_t)len_out[0] + 1);

        /* Wait for response or timeout */
        while (1) {
            int16_t wait_result;

            /*
             * Wait on the socket EC and the clock.  Arguments are pushed
             * right-to-left, so the pointers land at the lower addresses
             * (00e12b42 - 00e12b5e):
             *   00e12b42  pea (0x1).w                 vals[2] = 1
             *   00e12b46  move.l (-0x5c,A6),-(SP)     vals[1] = timeout_val
             *   00e12b4a  move.l D2,-(SP)             vals[0] = wait_val
             *   00e12b4c  pea (A3)   A3 = 0           ecs[2] = NULL
             *   00e12b4e  move.l #0xe2b0d4,-(SP)      ecs[1] = &TIME_$CLOCKH
             *   00e12b54  movea.l (-0x88,A6),A0       ; &SOCK_EC_TABLE[sock]
             *   00e12b58  movea.l (-0x4,A0),A1
             *   00e12b5c  pea (A1)                    ecs[0] = sock_ec
             *   00e12b5e  jsr EC_$WAIT
             *   00e12b68  tst.w D0w / seq D7b / bmi   ; loop while index == 0
             * ecs[2] is NULL, so vals[2] (1) is never looked at; it is kept
             * here for fidelity.  The socket EC entry is re-read from the
             * table on every pass.
             */
            wait_result = EC_$WAIT(
                (ec_$wait_ecs_t){{ SOCK_$EVENT_COUNTERS[sock_num - 1],
                                   (ec_$eventcount_t *)&TIME_$CLOCKH,
                                   NULL }},
                (ec_$wait_vals_t){{ wait_val, timeout_val, 1 }});

            if (wait_result != 0) {
                /* Timeout or other event */
                break;
            }

            /* Next expected socket EC value (00e12ae4 addq.l #0x1,D2) */
            wait_val++;

            /* Receive the response */
            APP_$RECEIVE(sock_num, &recv_pkt, status_ret);
            if (*status_ret != status_$ok) {
                break;
            }

            /* Extract response info */
            recv_len = *(uint16_t *)((char *)recv_pkt + 4);
            recv_id = *(int16_t *)((char *)recv_pkt + 6);
            recv_ppn = *(uint32_t *)((char *)recv_pkt + 8) & 0xFFFFFC00;

            /* Return the header buffer */
            NETBUF_$RTN_HDR(&recv_ppn);

            /* Check for data buffers and release them */
            data_buffers[0] = *(uint32_t *)((char *)recv_pkt + 16);
            if (data_buffers[0] != 0) {
                PKT_$DUMP_DATA(data_buffers, recv_len);
            }

            /* Check if response matches our request */
            if (recv_id == request_id) {
                result = (int8_t)0xFF;  /* Got response - node is reachable */
                goto done;
            }

            /* Wrong ID - continue waiting */
        }

        retry_count--;
    }

done:
    /* Close the socket */
    SOCK_$CLOSE(sock_num);

    /* Update visibility tracking */
    PKT_$NOTE_VISIBLE(dest_node, result);

    if (*status_ret != status_$ok) {
        return result;
    }

    if (result < 0) {
        /* Node responded */
        return result;
    }

    /* Node didn't respond */
    *status_ret = status_$network_remote_node_failed_to_respond;
    return result;
}
