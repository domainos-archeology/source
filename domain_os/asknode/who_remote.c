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
extern ec_$eventcount_t *DAT_00e28dc4;  /* Socket 5 EC */

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
    if (initial_count >= max_nodes || (DAT_00e24c3f & 1) == 0) {
        return;
    }

    /* Determine protocol version for request */
    uint16_t req_version;
    if (DAT_00e82426 == 3) {
        req_version = 2;
    } else {
        req_version = 3;
    }

    /* Determine target node */
    int32_t target_node = NODE_$ME;
    int32_t target_node_param = *node_id;

    /* Determine routing port */
    routing_port = *port;
    uint16_t pkt_len = 0x10;
    if (routing_port == -1) {
        if (is_local < 0) {
            routing_port = ROUTE_$PORT;
        } else {
            routing_port = DIR_$FIND_NET(0x29C, (int16_t)node_id);
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

    /* Open socket 5 for receiving WHO responses */
    if (SOCK_$OPEN(ASKNODE_WHO_SOCKET, 0x200000, 0) >= 0) {
        *status = status_$network_conflict_with_another_node_listing;
        return;
    }

    /* Get the event count for socket 5 */
    ec_$eventcount_t *socket_ec = DAT_00e28dc4;
    int32_t wait_val;

    /* Build request based on local/remote */
    uint32_t request[6];
    int16_t req_type;

    if (is_local < 0 || routing_port == 0 || routing_port == (int32_t)ROUTE_$PORT) {
        /* Local or direct query */
        req_type = 0;  /* Simple WHO */
        request[1] = 0;  /* No specific target */

        if (is_local < 0) {
            pkt_len = 0x90;
            target_node_param = 2;
            request[2] = max_nodes - 1;
        } else {
            request[2] = max_nodes;
        }
    } else {
        /* Remote query via gateway */
        req_type = 0x2D;  /* Remote WHO */
        request[1] = NODE_$ME;  /* Reply to us */
        request[2] = ROUTE_$PORT;
        request[3] = max_nodes;
        request[4] = -1;  /* flags */
        target_node = *node_id;
        request[5] = 0x4000;  /* timeout */
    }

    /* Generate packet ID */
    int16_t pkt_id = PKT_$NEXT_ID();
    wait_val = *(int32_t *)socket_ec;

    /* Copy packet info block */
    uint32_t pkt_info[8];
    {
        uint32_t *src = &DAT_00e82408;
        uint32_t *dst = pkt_info;
        int i;
        for (i = 0; i < 7; i++) *dst++ = *src++;
        *(uint16_t *)dst = *(uint16_t *)src;
    }
    *(uint16_t *)pkt_info = pkt_len;

    /* Build request header */
    request[0] = (req_version << 16) | req_type;

    /* Send WHO query */
    uint8_t temp1[2], temp2[4];
    status_$t send_status;
    PKT_$SEND_INTERNET(routing_port, target_node_param, 4, -1, NODE_$ME,
                       ASKNODE_WHO_SOCKET, pkt_info, pkt_id,
                       request, 0x18,
                       &DAT_00e658cc, 0,  /* No data */
                       temp1, temp2, &local_status);

    if (local_status != 0) {
        *status = local_status;
        SOCK_$CLOSE(ASKNODE_WHO_SOCKET);
        return;
    }

    /* Calculate quit check value */
    int32_t quit_val = *(int32_t *)((char *)&FIM_$QUIT_VALUE + PROC1_$AS_ID * 4) + 1;

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
        int32_t pkt_ptr;
        int32_t timeout_val;

        wait_val++;

        ecs[0] = socket_ec;
        ecs[1] = &TIME_$CLOCKH;
        ecs[2] = (ec_$eventcount_t *)((char *)&FIM_$QUIT_EC + PROC1_$AS_ID * 12);

        /* Calculate dynamic timeout based on CLOCKH plus port delay */
        timeout_val = *(int32_t *)&TIME_$CLOCKH + temp2[0] + 0x14;

        wait_result = EC_$WAIT(ecs, (uint32_t *)&wait_val);

        if (wait_result == 1) {
            /* Timeout */
            local_status = status_$network_waited_too_long_for_more_node_responses;
            break;
        }
        if (wait_result == 2) {
            /* Quit signal */
            *(int32_t *)((char *)&FIM_$QUIT_VALUE + PROC1_$AS_ID * 4) =
                *(int32_t *)((char *)&FIM_$QUIT_EC + PROC1_$AS_ID * 12);
            local_status = status_$network_quit_fault_during_node_listing;
            break;
        }

        /* Receive packet */
        APP_$RECEIVE(ASKNODE_WHO_SOCKET, &pkt_ptr, &local_status);

        if (local_status != 0) {
            continue;
        }

        /* Process response */
        {
            char *pkt_data = (char *)pkt_ptr;
            uint16_t pkt_len;
            int16_t resp_id;
            asknode_who_response_t response;

            pkt_len = *(uint16_t *)(pkt_data + 2);
            if (pkt_len > 0x200) pkt_len = 0x200;
            resp_id = *(int16_t *)(pkt_data + 6);

            /* Copy response data */
            OS_$DATA_COPY(pkt_data + 0x10, (char *)&response, pkt_len);
            NETBUF_$RTN_HDR((void **)&pkt_data);

            PKT_$DUMP_DATA((uint32_t *)(pkt_data + 0x1C), *(uint16_t *)(pkt_data + 4));

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
                /* Sequential response - check for duplicates */
                int16_t i;
                int found = 0;
                for (i = 0; i < initial_count; i++) {
                    if (node_list[i] == (int32_t)response.node_id) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    initial_count++;
                    node_list[initial_count - 1] = response.node_id;
                }
            }
        }
    }

    SOCK_$CLOSE(ASKNODE_WHO_SOCKET);
    *count = initial_count;
    *status = local_status;
}
