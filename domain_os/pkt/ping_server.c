/*
 * PKT_$PING_SERVER - Ping server process entry point
 *
 * This is the main loop for the ping server process that responds
 * to network ping requests. It opens a socket on PKT_PING_SOCKET (0x0D),
 * then loops forever receiving ping requests and sending responses.
 *
 * The server:
 * 1. Opens socket 0x0D for ping service
 * 2. Sets lock 0x13 (prevents preemption during receive)
 * 3. Loops waiting for packets on the socket's event count
 * 4. For each received packet:
 *    a. Extracts source address and request info
 *    b. Modifies flags to indicate response
 *    c. Sends response back to source
 *
 * Original address: 0x00E12BB8
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

/*
 * Error status for socket open failure
 */
static status_$t sock_open_error = 0x0011000C;  /* No socket available */

/*
 * Response template - minimal 2 bytes
 */
static uint8_t ping_response_template[2] = { 0, 0 };

void PKT_$PING_SERVER(void)
{
    int8_t result;
    status_$t status;
    int32_t wait_val;
    ec_$eventcount_t *sock_ec;
    void *recv_pkt;
    char template_buf[4];
    uint16_t template_len;
    uint32_t routing_key;
    uint32_t src_node_or;
    uint16_t src_sock;
    uint16_t dest_sock;
    uint8_t flags;
    int16_t request_id;
    uint32_t recv_ppn;
    uint32_t data_buffers[10];
    uint16_t len_out[5];
    uint16_t info_out[4];

    /* Open socket for ping service */
    result = SOCK_$OPEN(PKT_PING_SOCKET, 0x30000, PKT_CHUNK_SIZE);
    if (result >= 0) {
        /* Socket open failed */
        CRASH_SYSTEM(&sock_open_error);
    }

    /* Set lock to prevent preemption during receive processing */
    PROC1_$SET_LOCK(0x13);

    /*
     * Get socket's event count.  The original hard-codes the table slot
     * for socket 0xD:
     * 00e12bfe  movea.l (0x00e28de4).l,A0
     * 00e12c04  move.l (A0),D2
     * 00e12c0c  addq.l #0x1,D2
     * 0xE28DE4 == 0xE28DB4 + 0xD-1 entries, i.e.
     * SOCK_$EVENT_COUNTERS[PKT_PING_SOCKET - 1].
     */
    sock_ec = SOCK_$EVENT_COUNTERS[PKT_PING_SOCKET - 1];

    wait_val = sock_ec->value + 1;  /* Initial wait value */

    /* Main server loop - runs forever */
    for (;;) {
        /*
         * Wait for a packet to arrive.  Only one eventcount is used;
         * ecs[1] terminates the list, so vals[1]/vals[2] are ignored - but
         * the original still pushes 1 into both of them:
         *   00e12c28  pea (0x1).w              vals[2] = 1
         *   00e12c2c  move.l (SP),-(SP)        vals[1] = 1 (copy of vals[2])
         *   00e12c2e  move.l D2,-(SP)          vals[0] = wait_val
         *   00e12c30  pea (A2)   A2 = 0        ecs[2] = NULL
         *   00e12c32  pea (A2)                 ecs[1] = NULL
         *   00e12c34  movea.l (0x00e28de4).l,A0
         *   00e12c3a  pea (A0)                 ecs[0] = sock_ec
         *   00e12c3c  jsr EC_$WAIT
         * The result is discarded.
         */
        EC_$WAIT((ec_$wait_ecs_t){{ SOCK_$EVENT_COUNTERS[PKT_PING_SOCKET - 1],
                                    NULL, NULL }},
                 (ec_$wait_vals_t){{ wait_val, 1, 1 }});

        /* Receive the packet */
        APP_$RECEIVE(PKT_PING_SOCKET, &recv_pkt, &status);

        if (status != status_$ok) {
            /* Receive failed - wait for next packet */
            continue;
        }

        /* Extract packet info */
        routing_key = *(uint32_t *)((char *)recv_pkt + 0x18);  /* Saved routing key */
        template_len = *(uint16_t *)((char *)recv_pkt + 4);
        src_node_or = *(uint32_t *)((char *)recv_pkt + 0x0E);
        src_sock = *(uint16_t *)((char *)recv_pkt + 0x12);
        flags = *(uint8_t *)((char *)recv_pkt + 0x14);
        request_id = *(int16_t *)((char *)recv_pkt + 6);
        dest_sock = *(uint16_t *)((char *)recv_pkt + 2);

        /* Copy template data (up to 2 bytes for ping) */
        template_len = 2;
        if (dest_sock < 2) {
            template_len = dest_sock;
        }
        OS_$DATA_COPY(*(char **)((char *)recv_pkt + 0x28), template_buf, (uint32_t)template_len);

        /* Return the packet header buffer */
        recv_ppn = (*(uint32_t *)((char *)recv_pkt + 0x28)) & 0xFFFFFC00;
        NETBUF_$RTN_HDR(&recv_ppn);

        /* Release data buffers if present */
        data_buffers[0] = *(uint32_t *)((char *)recv_pkt + 0x2C);
        if (data_buffers[0] != 0) {
            PKT_$DUMP_DATA(data_buffers, template_len);
        }

        /* Modify flags for response:
         * Clear bits 7 (0x80) and 4 (0x10), set bit 5 (0x20)
         * This marks it as a response rather than request
         */
        PKT_$DATA->ping_server_flags = (flags & 0xFF6F) | 0x20;

        /*
         * Send ping response back to source.
         * TODO(source-f3py): 0x00E12CEA - 0x00E12D24 pushes &pkt_$no_data for
         * the "data" argument (0x00E12CFA "pea (-0x148,PC)"), &PKT_$DATA->
         * ping_req_hdr for the template (0x00E12D02 "pea (0x5a,A5)"), and two
         * distinct word locals for retry_hint/timeout_out (0x00E12CF4 /
         * 0x00E12CF0).  This call passes NULL for three of them.
         */
        PKT_$SEND_INTERNET(routing_key, src_node_or, src_sock,
                           (int32_t)-1, NODE_$ME, PKT_PING_SOCKET,
                           &PKT_$DATA->ping_server_flags, request_id,
                           ping_response_template, 2,
                           NULL, 0,
                           len_out, NULL, &status);

        /* Increment wait value for next iteration */
        wait_val++;
    }
}
