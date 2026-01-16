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
    uint32_t *sock_ec;
    int32_t wait_val;
    int32_t timeout_val;
    uint16_t len_out[5];
    status_$t local_status;
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
        /* TODO: Access ROUTE_$PORTP[port] to get port info and check type at offset 0x2E */
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

    /* Get the socket's event count for waiting */
    /* TODO: Access socket event count array */
    /* sock_ec = *(uint32_t **)(&SOCK_EC_ARRAY + sock_num * 4); */
    /* wait_val = *sock_ec + 1; */

    while (retry_count >= 0) {
        /* Send ping request */
        PKT_$SEND_INTERNET(routing_key, dest_node, PKT_PING_SOCKET,
                           (int32_t)-1, NODE_$ME, sock_num,
                           &ping_request_info, request_id,
                           ping_template, 2,
                           NULL, 0,
                           len_out, NULL, &local_status);

        if (local_status != status_$ok) {
            break;
        }

        /* Calculate timeout */
        timeout_val = TIME_$CLOCKH + (uint32_t)len_out[0] + 1;

        /* Wait for response or timeout */
        while (1) {
            ec_$eventcount_t *ecs[3];
            int32_t wait_vals[3];
            int16_t wait_result;

            /* Set up event counts to wait on:
             * - Socket receive event count
             * - Time event count for timeout
             */
            /* TODO: Set up proper EC wait */
            /* wait_result = EC_$WAIT(ecs, wait_vals); */
            wait_result = 1;  /* Simulate timeout for now */

            if (wait_result != 0) {
                /* Timeout or other event */
                break;
            }

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
