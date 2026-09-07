/*
 * ASKNODE_$WHO_NOTOPO - List nodes without topology support
 *
 * Lists network nodes using broadcast queries rather than topology-based
 * routing. Used as fallback when topology information is not available.
 *
 * This function:
 * 1. Allocates a socket for receiving responses
 * 2. Sends a WHO broadcast query
 * 3. Waits for responses with timeout
 * 4. Collects responding node IDs until max_count reached or timeout
 *
 * Original address: 0x00E65FDC
 * Size: 856 bytes
 */

#include "asknode/asknode_internal.h"

/* ASKNODE_$EMPTY_DATA (0xE658CC) is declared in asknode_internal.h */

void ASKNODE_$WHO_NOTOPO(int32_t *node_id, int32_t *port,
                         int32_t *node_list, int16_t *max_count,
                         uint16_t *count, status_$t *status)
{
    int16_t max_nodes;
    int8_t is_local;
    int32_t routing_port;
    int16_t port_idx;
    uint16_t sock_num;
    ec_$eventcount_t *socket_ec;
    int32_t wait_val;
    int16_t pkt_id;
    int32_t timeout_end;
    int32_t quit_val;
    uint16_t resp_timeout;      /* A6-0x2B8: PKT_$SEND_INTERNET's timeout out */
    status_$t local_status[5];
    ec_$eventcount_t *ecs[3];
    int32_t local_node;

    /* Initialize outputs */
    *status = 0;
    *count = 0;

    max_nodes = *max_count;
    if (max_nodes <= 0) {
        return;
    }

    /* Limit to maximum */
    if (max_nodes > ASKNODE_MAX_WHO_COUNT) {
        max_nodes = ASKNODE_MAX_WHO_COUNT;
    }

    /* Check if querying local node */
    is_local = (*node_id == 0) || (*node_id == (int32_t)NODE_$ME) ? -1 : 0;

    /* Determine routing port */
    routing_port = *port;
    if (routing_port == -1) {
        if (is_local < 0) {
            routing_port = ROUTE_$PORT;
        } else {
            /*
             *   00e66044    pea (A3)                 ; node_id (pointer)
             *   00e66046    move.l #0xe8029c,-(SP)   ; &NAME_$ROOT_UID
             */
            routing_port = DIR_$FIND_NET(&NAME_$ROOT_UID, (uint32_t *)node_id);
        }
    }

    /* Find next hop for routing */
    {
        uint16_t nexthop_port;
        uint8_t nexthop_addr[16];
        int32_t addr_info[2];

        addr_info[0] = routing_port;
        addr_info[1] = 1;  /* flags */

        RIP_$FIND_NEXTHOP(addr_info, 0, &port_idx, nexthop_addr, local_status);
    }

    if (local_status[0] != 0) {
        *status = local_status[0];
        return;
    }

    /* If querying local or direct route, add local node to list */
    if (is_local < 0 || port_idx == 0) {
        *count = 1;
        node_list[0] = NODE_$ME;
        local_node = 0;
    } else {
        local_node = *node_id;
    }

    /* Allocate a socket for receiving responses */
    if (SOCK_$ALLOCATE(&sock_num, 0x200020, 0) >= 0) {
        *status = status_$network_no_more_free_sockets;
        return;
    }

    /*
     * Get the event count for this socket: entry sock_num of the socket
     * pointer table at 0xE28DB0 (slot 0 = spinlock), i.e.
     * SOCK_$EVENT_COUNTERS[sock_num - 1] (SOCK_$EVENT_COUNTERS is 0xE28DB4).
     */
    socket_ec = SOCK_$EVENT_COUNTERS[sock_num - 1];
    wait_val = EC_$READ(socket_ec) + 1;

    /* Build WHO request packet */
    {
        uint32_t request[6];
        uint32_t pkt_info[8];
        uint16_t retry_hint;    /* A6-0x2BA */

        request[0] = 0x00030045;  /* Version 3, request type 0x45 (WHO) */
        request[1] = NODE_$ME;
        /* Get port info from ROUTE_$PORTP */
        request[2] = *(uint32_t *)(*((void **)&ROUTE_$PORTP + port_idx));
        request[3] = 0x5B8D8;  /* Magic constant (timeout related) */

        pkt_id = PKT_$NEXT_ID();

        /* Copy packet info block */
        {
            uint32_t *src = PKT_$DEFAULT_INFO;
            uint32_t *dst = pkt_info;
            int i;
            for (i = 0; i < 7; i++) *dst++ = *src++;
            *(uint16_t *)dst = *(uint16_t *)src;
        }
        pkt_info[2] = 0;  /* Clear flags */
        *(uint16_t *)pkt_info = 0x90;  /* Packet length */

        /* Send WHO query */
        PKT_$SEND_INTERNET(routing_port, local_node, 4, -1, NODE_$ME,
                           sock_num, pkt_info, pkt_id,
                           request, 0x18,
                           &ASKNODE_$EMPTY_DATA, 0,  /* No data */
                           &retry_hint, &resp_timeout, local_status);
    }

    if (local_status[0] != 0) {
        *status = local_status[0];
        SOCK_$CLOSE(sock_num);
        return;
    }

    /*
     * 0x00E661B8 - 0x00E661E8: the deadline is built from the timeout word
     * PKT_$SEND_INTERNET returned (A6-0x2B8), zero-extended:
     *   andi.l #0xFFFF,D5 / add.l EC_$READ(&TIME_$CLOCKH),D5 / addq.l #6,D5
     */
    timeout_end = EC_$READ((ec_$eventcount_t *)&TIME_$CLOCKH) +
                  (int32_t)resp_timeout + 6;
    quit_val = (int32_t)FIM_$QUIT_VALUE[PROC1_$AS_ID] + 1;

    /* Wait for responses */
    while (1) {
        int16_t wait_result;
        app_$receive_rec_t rcv;     /* A6-0x30, 44 bytes */


        /*
         * 0x00E661EC - 0x00E6621A.  Both arrays are pushed by value:
         *   ecs  = { socket EC (A3), &TIME_$CLOCKH, &FIM_$QUIT_EC[as_id] }
         *   vals = { D4 (socket value), A6-0x28C (deadline),
         *            A6-0x290 (FIM_$QUIT_VALUE[as_id] + 1) }
         * Only D4 is advanced by the loop ("addq.l #1,D4" at 0x00E662E8).
         */
        ecs[0] = socket_ec;
        ecs[1] = (ec_$eventcount_t *)&TIME_$CLOCKH;
        ecs[2] = &FIM_$QUIT_EC[PROC1_$AS_ID];

        wait_result = EC_$WAIT((ec_$wait_ecs_t){{ ecs[0], ecs[1], ecs[2] }},
                               (ec_$wait_vals_t){{ wait_val, timeout_end,
                                                   quit_val }});

        if (wait_result == 1) {
            /* Timeout */
            break;
        }
        if (wait_result != 0 && wait_result != 2) {
            /* 0x00E66238: any other index leaves the loop as well */
            break;
        }
        if (wait_result == 2) {
            /* Quit signal */
            FIM_$QUIT_VALUE[PROC1_$AS_ID] = (uint32_t)FIM_$QUIT_EC[PROC1_$AS_ID].value;
            *status = status_$network_quit_fault_during_node_listing;
            break;
        }

        /* 0x00E6623C: APP_$RECEIVE(sock_num, &rcv, local_status) */
        APP_$RECEIVE(sock_num, &rcv, local_status);

        /* 0x00E66252: a failed receive goes straight back to the wait */
        if (local_status[0] != 0) {
            continue;
        }

        /* Process response */
        {
            uint16_t pkt_len;
            int16_t resp_id;
            uint16_t dump_len;
            asknode_response_t response;
            asknode_$reply_hdr_t *reply =
                (asknode_$reply_hdr_t *)ARCH_VA_TO_PTR(rcv.reply);

            /*
             * 0x00E66258-0x00E66284, in the original's order.  Everything the
             * reply record is read for happens BEFORE the buffer goes back.
             */

            /* 0x00E6625C: keep the payload byte count for PKT_$DUMP_DATA */
            dump_len = reply->data_len;

            /* 0x00E66262-0x00E6626A: the responding node id, read here */
            node_list[*count] = (int32_t)reply->node_id;

            /* 0x00E66270: reply length, clamped to 0x200 */
            pkt_len = reply->length;
            if (pkt_len > 0x200) {
                pkt_len = 0x200;
            }

            /* 0x00E66280 */
            resp_id = reply->reply_id;

            /*
             * 0x00E66286-0x00E66296: the copy source is the record's DATA
             * pointer (record+4), not an offset into the reply record.
             */
            OS_$DATA_COPY((char *)ARCH_VA_TO_PTR(rcv.data),
                          (char *)&response, pkt_len);

            /* 0x00E6629A: return the buffer that data pointer names */
            NETBUF_$RTN_HDR(&rcv.data);

            /*
             * 0x00E662A6-0x00E662B6: the page vector is the record's own
             * (A6-0x28), which is still valid after the header went back.
             */
            PKT_$DUMP_DATA(rcv.data_pages, dump_len);

            if (local_status[0] != 0) {
                *status = local_status[0];
                break;
            }

            /* Check if this is our response (matching packet ID) */
            if (pkt_id == resp_id) {
                if (response.status == 0) {
                    (*count)++;
                    if ((int16_t)*count >= max_nodes) {
                        break;
                    }
                } else {
                    *status = response.status;
                }
            }
        }

        wait_val++;
    }

    SOCK_$CLOSE(sock_num);
}
