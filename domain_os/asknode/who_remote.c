/*
 * ASKNODE_$WHO_REMOTE - List nodes using remote topology
 *
 * Lists network nodes using the network topology for efficient
 * routing of WHO queries. Supports multi-hop networks by leveraging
 * topology information to route queries through gateways.
 *
 * This function:
 * 1. Opens socket 5 for WHO responses
 * 2. Sends a WHO query packet (type 0 or 0x2D based on local/remote)
 * 3. Waits for responses from remote nodes
 * 4. Collects node IDs from responses
 *
 * Original address: 0x00E66334
 * Size: 1028 bytes
 */

#include "asknode/asknode_internal.h"

/* External references */

void ASKNODE_$WHO_REMOTE(int32_t *node_id, int32_t *port,
                         int32_t *node_list, int16_t *max_count,
                         uint16_t *count, status_$t *status)
{
    int16_t max_nodes;
    int8_t is_local;
    int32_t routing_port;
    uint16_t initial_count;
    status_$t local_status = 0;

    /* Initialize outputs */
    local_status = 0;
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

    /* If local, add ourselves to the list */
    if (is_local < 0) {
        node_list[0] = NODE_$ME;
        *count = 1;
    } else {
        *count = 0;
    }
    initial_count = *count;

    /* Check if we should continue (need more nodes and network enabled) */
    if (initial_count >= max_nodes || (NETWORK_$CAPABLE_FLAGS & 1) == 0) {
        return;
    }

    /* Determine protocol version for request */
    uint16_t req_version;
    if (ASKNODE_$DATA.protocol_version == 3) {
        req_version = 2;
    } else {
        req_version = 3;
    }

    /*
     * 0x00E663D2 / 0x00E663DA: the request's target node starts as NODE_$ME
     * and PKT_$SEND_INTERNET's destination as *node_id; the remote form
     * overwrites the first and the local form the second.
     */
    int32_t target_node = (int32_t)NODE_$ME;
    int32_t target_node_param = *node_id;

    /* Determine routing port */
    routing_port = *port;
    uint16_t pkt_len = 0x10;
    if (routing_port == -1) {
        if (is_local < 0) {
            routing_port = ROUTE_$PORT;
        } else {
            /*
             *   00e663fa    pea (A3)                 ; node_id (pointer)
             *   00e663fc    move.l #0xe8029c,-(SP)   ; &NAME_$ROOT_UID
             */
            routing_port = DIR_$FIND_NET(&NAME_$ROOT_UID, (uint32_t *)node_id);
        }
    }

    /* Check network capability */
    {
        int16_t cap_result = ROUTE_$VALIDATE_PORT(routing_port, is_local);
        if (cap_result == 2) {
            *status = status_$network_operation_not_defined_on_hardware;
            return;
        }
    }

    /*
     * Open socket 5 for receiving WHO responses.  The flag longword is
     * 0x00200020 ("move.l #0x200020,-(SP)" at 0x00E66434) - the same value
     * ASKNODE_$WHO_NOTOPO hands SOCK_$ALLOCATE - not 0x200000.
     */
    if (SOCK_$OPEN(ASKNODE_WHO_SOCKET, 0x200020, 0) >= 0) {
        *status = status_$network_conflict_with_another_node_listing;
        return;
    }

    /* Get the event count for socket 5 */
    /* Socket 5 event count: table slot 5 (0xE28DC4) = SOCK_$DATA.socket_ptr[5] */
    ec_$eventcount_t *socket_ec = &SOCK_$DATA.socket_ptr[ASKNODE_WHO_SOCKET]->ec;
    int32_t wait_val;

    /*
     * Build the request at A6-0x268 (0x00E663BC - 0x00E664BC).  It is an
     * asknode_request_t (asknode_internal.h) and, like every Pascal variant
     * record here, the two forms overlay different shapes on the same words:
     *
     *   +0x00 version        2 when ASKNODE_$DATA.protocol_version == 3, else 3
     *   +0x02 request_type   0 (simple WHO) or 0x2D (remote WHO)
     *   +0x04 node_id        NODE_$ME (0x00E663D2), overwritten with
     *                        *node_id in the remote form (0x00E664B8)
     *   simple form:
     *   +0x08 a WORD holding max_nodes, or max_nodes - 1 when this is the
     *         local query (0x00E6648A / 0x00E66490)
     *   remote form:
     *   +0x08 long NODE_$ME     (0x00E6649E)
     *   +0x0C long ROUTE_$PORT  (0x00E664A6)
     *   +0x10 byte 0xFF, "st"   (0x00E664B4) - the forwarded flag
     *   +0x12 word max_nodes    (0x00E664AE)
     *   +0x14 long 0x4000       (0x00E664BC)
     *
     * The tree used to write these one slot lower and as longwords
     * throughout, which shifted every field of the remote form.
     */
    asknode_request_t request;

    request.version = req_version;
    request.node_id = (uint32_t)target_node;

    if (is_local < 0 || routing_port == 0 ||
        routing_port == (int32_t)ROUTE_$PORT) {
        /* Simple WHO: the responder enumerates for us. */
        request.request_type = ASKNODE_REQ_WHO;      /* 0 */

        if (is_local < 0) {
            /* 0x00E6647A - 0x00E6648A */
            pkt_len = 0x90;
            target_node_param = 2;
            *(uint16_t *)((uint8_t *)&request + 0x08) =
                (uint16_t)(max_nodes - 1);
        } else {
            /* 0x00E66490 */
            *(uint16_t *)((uint8_t *)&request + 0x08) = (uint16_t)max_nodes;
        }
    } else {
        /* Remote WHO through a gateway. */
        request.request_type = ASKNODE_REQ_WHO_REMOTE;   /* 0x2D */
        request.param1 = NODE_$ME;      /* +0x08: where to reply */
        request.param2 = ROUTE_$PORT;   /* +0x0C */
        request.count = max_nodes;      /* +0x12 */
        request.forwarded = (int8_t)0xFF;
        request.node_id = (uint32_t)*node_id;   /* +0x04, after the default */
        request.param3 = 0x4000;        /* +0x14: timeout */
    }

    /* Generate packet ID */
    int16_t pkt_id = PKT_$NEXT_ID();
    wait_val = *(int32_t *)socket_ec;

    /* Copy packet info block */
    uint32_t pkt_info[8];
    {
        const uint32_t *src = (const uint32_t *)&ASKNODE_$DATA.pkt_info;
        uint32_t *dst = pkt_info;
        int i;
        for (i = 0; i < 7; i++) *dst++ = *src++;
        *(uint16_t *)dst = *(uint16_t *)src;
    }
    *(uint16_t *)pkt_info = pkt_len;

    /* Send WHO query */
    uint16_t retry_hint;        /* A6-0x28A */
    uint16_t resp_timeout;      /* A6-0x288, read into D7 at 0x00E6652C */
    status_$t send_status;
    PKT_$SEND_INTERNET(routing_port, target_node_param, 4, -1, NODE_$ME,
                       ASKNODE_WHO_SOCKET, pkt_info, pkt_id,
                       &request, 0x18,
                       &ASKNODE_$EMPTY_DATA, 0,  /* No data */
                       &retry_hint, &resp_timeout, &local_status);

    /*
     * 0x00E66530: a failed send branches to 0x00E66714, the common tail that
     * closes the socket and writes *count and *status - it does not have an
     * exit of its own.
     */
    if (local_status != 0) {
        SOCK_$CLOSE(ASKNODE_WHO_SOCKET);
        *count = initial_count;
        *status = local_status;
        return;
    }

    /* Calculate quit check value */
    int32_t quit_val = (int32_t)FIM_$QUIT_VALUE[PROC1_$AS_ID] + 1;

    /* Pre-clear remaining slots in node list */
    {
        int16_t i;
        for (i = initial_count + 1; i <= max_nodes; i++) {
            node_list[i - 1] = 0;
        }
    }

    /* Wait for responses */
    while (initial_count < max_nodes) {
        int16_t wait_result;
        ec_$eventcount_t *ecs[3];
        app_$receive_rec_t rcv;     /* A6-0x30, 44 bytes */
        int32_t timeout_val;

        wait_val++;

        /*
         * 0x00E6657E - 0x00E665C0.  Both arrays go by value:
         *   ecs  = { A6-0x26C (socket EC), A3 (= &TIME_$CLOCKH, loaded at
         *            0x00E66572), &FIM_$QUIT_EC[as_id] }
         *   vals = { D5 (socket value, bumped at 0x00E6657E),
         *            TIME_$CLOCKH + D7 + 0x14, A6-0x274 (quit value + 1) }
         * D7 carries PKT_$SEND_INTERNET's timeout on the first pass only; it
         * is cleared right after the wait ("clr.w D7w" at 0x00E665C0), so
         * later passes get a flat 0x14-tick deadline.
         */
        ecs[0] = socket_ec;
        ecs[1] = (ec_$eventcount_t *)&TIME_$CLOCKH;
        ecs[2] = &FIM_$QUIT_EC[PROC1_$AS_ID];

        timeout_val = (int32_t)TIME_$CLOCKH + (int32_t)resp_timeout + 0x14;

        wait_result = EC_$WAIT((ec_$wait_ecs_t){{ ecs[0], ecs[1], ecs[2] }},
                               (ec_$wait_vals_t){{ wait_val, timeout_val,
                                                   quit_val }});

        resp_timeout = 0;                       /* 0x00E665C0: clr.w D7w */

        if (wait_result == 1) {
            /* Timeout */
            local_status = status_$network_waited_too_long_for_more_node_responses;
            break;
        }
        if (wait_result == 2) {
            /* Quit signal */
            FIM_$QUIT_VALUE[PROC1_$AS_ID] = (uint32_t)FIM_$QUIT_EC[PROC1_$AS_ID].value;
            local_status = status_$network_quit_fault_during_node_listing;
            break;
        }

        /* 0x00E6661C */
        APP_$RECEIVE(ASKNODE_WHO_SOCKET, &rcv, &local_status);

        /* 0x00E66626: a failed receive goes back to the wait */
        if (local_status != 0) {
            continue;
        }

        /* Process response */
        {
            uint16_t pkt_len;
            int16_t resp_id;
            uint16_t dump_len;
            asknode_who_response_t response;
            asknode_$reply_hdr_t *reply =
                (asknode_$reply_hdr_t *)ARCH_VA_TO_PTR(rcv.reply);

            /*
             * 0x00E6662E-0x00E6667E, in the original's order: the reply
             * record is fully read before the header buffer goes back, and
             * PKT_$DUMP_DATA gets the receive record's own page vector at
             * A6-0x28 rather than an offset into the freed buffer.
             */

            /* 0x00E66632 */
            dump_len = reply->prefix.data_len;

            /* 0x00E66638: length, clamped to 0x200 */
            pkt_len = reply->prefix.template_len;
            if (pkt_len > 0x200) {
                pkt_len = 0x200;
            }

            /* 0x00E66648 */
            resp_id = reply->prefix.request_id;

            /* 0x00E6664E-0x00E6665E: source is the record's data pointer */
            OS_$DATA_COPY((char *)ARCH_VA_TO_PTR(rcv.data),
                          (char *)&response, pkt_len);

            /* 0x00E66662 */
            NETBUF_$RTN_HDR(&rcv.data);

            /* 0x00E6666E-0x00E6667E */
            PKT_$DUMP_DATA(rcv.data_pages, dump_len);

            local_status = response.status;

            /* Validate response */
            if (local_status != 0) {
                continue;
            }
            if (pkt_id != resp_id) {
                continue;
            }

            /* Check response type */
            if (response.response_type != 1 && response.response_type != 0x2E) {
                continue;
            }

            /* Check if we found ourselves (done for local query) */
            if (response.node_id == NODE_$ME && response.response_type == 1) {
                break;
            }

            /* Check response flags */
            if (response.flags == 0xB1FF) {
                /* Indexed response - place at specific position */
                uint16_t pos = max_nodes - response.count + 1;
                if ((int16_t)pos == (int16_t)initial_count + 1) {
                    initial_count = pos;
                }
                if ((int16_t)pos <= (int16_t)initial_count) {
                    node_list[pos - 1] = response.node_id;
                }
            } else {
                /*
                 * Sequential response (0x00E666E4 - 0x00E6670A).  The scan
                 * for an already-listed node branches to 0x00E66714 - the
                 * function's EXIT - not to the loop condition: a node that
                 * answers twice ENDS the whole listing, it is not merely
                 * skipped.  That is how the broadcast form knows it has been
                 * all the way round the ring.
                 */
                int16_t i;
                int duplicate = 0;
                for (i = 0; i < (int16_t)initial_count; i++) {
                    if (node_list[i] == (int32_t)response.node_id) {
                        duplicate = 1;
                        break;
                    }
                }
                if (duplicate) {
                    break;
                }
                initial_count++;
                node_list[initial_count - 1] = response.node_id;
            }
        }
    }

    SOCK_$CLOSE(ASKNODE_WHO_SOCKET);
    *count = initial_count;
    *status = local_status;
}
