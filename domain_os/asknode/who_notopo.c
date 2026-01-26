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

/* External references - commented out as they conflict with header definitions */
/* extern ec_$eventcount_t TIME_$CLOCKH; */
/* extern int16_t PROC1_$AS_ID; */
/* extern uint32_t FIM_$QUIT_VALUE; */
/* extern ec_$eventcount_t FIM_$QUIT_EC; */

/* Empty data constant at 0x00E658CC (zero-filled buffer) */
extern uint32_t DAT_00e658cc;

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
            routing_port = DIR_$FIND_NET(0x29C, (int16_t)node_id);
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

    /* Get the event count for this socket */
    socket_ec = *(ec_$eventcount_t **)((char *)&sock_spinlock + sock_num * 4);
    wait_val = EC_$READ(socket_ec) + 1;

    /* Build WHO request packet */
    {
        uint32_t request[6];
        uint32_t pkt_info[8];
        uint8_t temp1[2], temp2[4];

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
                           &DAT_00e658cc, 0,  /* No data */
                           temp1, temp2, local_status);
    }

    if (local_status[0] != 0) {
        *status = local_status[0];
        SOCK_$CLOSE(sock_num);
        return;
    }

    /* Calculate timeout */
    timeout_end = EC_$READ(&TIME_$CLOCKH) + port_idx + 6;
    quit_val = *(int32_t *)((char *)&FIM_$QUIT_VALUE + PROC1_$AS_ID * 4) + 1;

    /* Wait for responses */
    while (1) {
        int16_t wait_result;
        int32_t pkt_ptr;
        char *pkt_data;

        ecs[0] = socket_ec;
        ecs[1] = &TIME_$CLOCKH;
        ecs[2] = (ec_$eventcount_t *)((char *)&FIM_$QUIT_EC + PROC1_$AS_ID * 12);

        wait_result = EC_$WAIT(ecs, (uint32_t *)&wait_val);

        if (wait_result == 1) {
            /* Timeout */
            break;
        }
        if (wait_result == 2) {
            /* Quit signal */
            *(int32_t *)((char *)&FIM_$QUIT_VALUE + PROC1_$AS_ID * 4) =
                *(int32_t *)((char *)&FIM_$QUIT_EC + PROC1_$AS_ID * 12);
            *status = status_$network_quit_fault_during_node_listing;
            break;
        }

        /* Receive packet */
        APP_$RECEIVE(sock_num, &pkt_ptr, local_status);

        if (local_status[0] != 0) {
            continue;
        }

        /* Process response */
        {
            uint16_t pkt_len;
            int16_t resp_id;
            asknode_response_t response;

            pkt_data = (char *)pkt_ptr;
            pkt_len = *(uint16_t *)(pkt_data + 2);
            if (pkt_len > 0x200) pkt_len = 0x200;
            resp_id = *(int16_t *)(pkt_data + 6);

            /* Copy response data */
            OS_$DATA_COPY(pkt_data + 0x10, (char *)&response, pkt_len);
            NETBUF_$RTN_HDR((void **)&pkt_data);

            /* Get node ID from response */
            node_list[*count] = *(int32_t *)(pkt_data + 0x0E);

            PKT_$DUMP_DATA((uint32_t *)(pkt_data + 0x1C), *(uint16_t *)(pkt_data + 4));

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
