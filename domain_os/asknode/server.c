/*
 * ASKNODE_$SERVER - Handle incoming node query requests
 *
 * Server function that processes incoming ASKNODE requests from other nodes.
 * Receives a request packet on socket 4, processes it based on request type,
 * and sends a response back.
 *
 * This is a complex server function that handles many different request
 * types by calling ASKNODE_$INTERNET_INFO for most, with special handling
 * for certain request types like WHO (0x00), WHO_REMOTE (0x2D), time sync
 * (0x45), failure recording (0x0E), and log reading (0x31).
 *
 * Original address: 0x00E6597A
 * Size: 1114 bytes
 */

#include "asknode/asknode_internal.h"

/* External references */

/* Network failure record globals */
extern uint8_t DAT_00e24bf6;   /* Failure record flag */
extern uint32_t DAT_00e24bf8;  /* Failure record word 1 */
extern uint32_t DAT_00e24bfc;  /* Failure record word 2 */
extern uint32_t DAT_00e24c00;  /* Failure record word 3 */

void ASKNODE_$SERVER(int16_t *response, int32_t *routing_info)
{
    status_$t status;
    int32_t pkt_ptr;
    char *pkt_data;
    asknode_request_t request;
    uint32_t pkt_info[8];
    uint8_t temp1[2], temp2[16];
    uint16_t pkt_len;
    int32_t src_node;
    int16_t src_port;
    int16_t request_id;
    uint8_t flags;
    int8_t should_propagate = 0;
    int32_t netbuf_handle = 0;
    void *netbuf_va = NULL;
    uint16_t response_len = 0x200;
    uint16_t data_len = 0;

    /* Receive a packet from socket 4 */
    APP_$RECEIVE(4, &pkt_ptr, &status);
    if (status != 0) {
        return;
    }

    /* Parse packet header */
    pkt_data = (char *)pkt_ptr;
    PKT_$DUMP_DATA((uint32_t *)(pkt_data + 0x1C), *(uint16_t *)(pkt_data + 4));

    src_node = *(int32_t *)(pkt_data + 8);
    *routing_info = *(int32_t *)(pkt_data + 0x14);

    /* Extract packet fields */
    {
        uint32_t src_info = *(uint32_t *)(pkt_data + 8);
        uint32_t flags_info = *(uint32_t *)(pkt_data + 0x0E);
        request_id = *(int16_t *)(pkt_data + 0x12);
        flags = *(uint8_t *)(pkt_data + 0x14);
        src_port = *(int16_t *)(pkt_data + 6);

        /* Copy request data (up to 0x18 bytes) */
        pkt_len = *(uint16_t *)(pkt_data + 2);
        if (pkt_len > 0x18) pkt_len = 0x18;
        OS_$DATA_COPY(pkt_data + 0x10, (char *)&request, pkt_len);
    }

    /* Return header buffer */
    NETBUF_$RTN_HDR((void **)&pkt_data);

    /* Set response type */
    response[1] = request.request_type + 1;
    if (request.version == 2) {
        response[0] = 2;
    } else {
        response[0] = 3;
    }

    /* Initialize response status */
    response[6] = 0;  /* Clear status high */
    response[7] = 0;  /* Clear status low */

    /*
     * Handle request based on type
     */
    switch (request.request_type) {
    case 0x00:
        /* WHO query - basic node enumeration */
        {
            int8_t local_flag = *(int8_t *)(pkt_data + 9);
            if (local_flag < 0) {
                /* Local query - ignore */
                return;
            }

            response[8] = NODE_$ME;
            response[10] = request.count;
            request.count--;
            response[0xF] = 0;
            response[0x10] = 0x1000;
            response[9] = 0xB1FF;  /* Response flags */

            /* Determine routing */
            if (src_node == 0) {
                *routing_info = *(int32_t *)(pkt_data + 0x14);
            } else {
                *routing_info = src_node;
            }

            /* Check network capability */
            response[7] = 0;
            {
                int16_t cap = FUN_00e65904(*routing_info, -1);
                if (cap == 2) {
                    response[7] = status_$network_operation_not_defined_on_hardware;
                } else if (cap == 0) {
                    response[7] = status_$network_unknown_network;
                }
            }

            /* Set propagation flag */
            should_propagate = (response[7] == 0) &&
                               (request.count > 0) &&
                               (request.node_id != NODE_$ME) &&
                               ((flags & 4) == 0) ? -1 : 0;
            response[1] = 0;
            request.node_id = request.node_id;  /* dest for propagate */
        }
        break;

    case 0x2D:
        /* WHO_REMOTE - remote node query through gateway */
        {
            int8_t local_flag = *(int8_t *)(pkt_data + 9);
            if (local_flag < 0) {
                return;
            }

            /* Check if we're the target or should forward */
            if (*(int8_t *)&request.count < 0 || request.node_id != NODE_$ME) {
                response[1] = 0x2E;  /* Forward response type */
                response[8] = NODE_$ME;
            } else {
                response[8] = request.param1;
                response[1] = 1;  /* Final response type */
            }

            response[9] = 0xB1FF;
            if (src_node == 0) {
                *routing_info = *(int32_t *)(pkt_data + 0x14);
            } else {
                *routing_info = src_node;
            }

            response[7] = 0;
            {
                int8_t is_local = (src_node == 0 || src_node == (int32_t)NODE_$ME) ? -1 : 0;
                int16_t cap = FUN_00e65904(*routing_info, is_local);
                if (cap == 2) {
                    response[7] = status_$network_operation_not_defined_on_hardware;
                } else if (cap == 0) {
                    response[7] = status_$network_unknown_network;
                }
            }

            response[10] = request.count - 1;
            *(int32_t *)(pkt_data + 0x14) = request.param2;  /* Update routing */
            response[0xF] = *(int16_t *)&request.param3;
            response[0x10] = *(int16_t *)((char *)&request.param3 + 2);

            /* Set propagation flag */
            should_propagate = (response[7] == 0) &&
                               (request.count - 1 > 0) &&
                               (*(int8_t *)&request.count || request.node_id != NODE_$ME) &&
                               ((flags & 4) == 0) ? -1 : 0;
            response[1] = 0x2D;
            request.node_id = request.param1;
        }
        break;

    case 0x45:
        /* Time sync WHO query */
        response[1] = 0x46;
        response[8] = NODE_$ME;
        response[7] = 0;
        response[9] = 0xB1FF;

        if (src_node == 0) {
            *routing_info = *(int32_t *)(pkt_data + 0x14);
        } else {
            *routing_info = src_node;
        }

        /* Get current time */
        TIME_$CLOCK((clock_t *)(response + 0xE));
        response[0xE] = 0;  /* Clear high word */

        /* Subtract provided time offset */
        {
            int32_t result = M_OIS_LLL(*(uint32_t *)(response + 0xF) & 0x7FFFFFFF,
                                       request.param3);
            *(int32_t *)(response + 0xF) = result;
        }

        should_propagate = -1;
        response[1] = 0x46;
        break;

    case 0x0E:
        /* Record network failure */
        DAT_00e24bf6 = 0xFF;
        DAT_00e24bf8 = request.param2;
        DAT_00e24bfc = TIME_$CURRENT_CLOCKH;
        DAT_00e24c00 = request.node_id;
        return;  /* No response needed */

    case 0x31:
        /* Log read request */
        NETBUF_$GET_DAT(&netbuf_handle);
        NETBUF_$GETVA(netbuf_handle, &netbuf_va, &status);
        response[7] = status;

        if (status == 0) {
            if ((request.node_id & 0x10000) == 0) {
                uint16_t log_len = request.node_id;
                if (log_len > 0x400) log_len = 0x400;
                LOG_$READ((int16_t)netbuf_va, log_len, (int16_t)&response[8]);
            } else {
                LOG_$READ2((uint16_t *)netbuf_va, (int16_t)request.node_id >> 16,
                           0x400, (uint32_t *)&response[8]);
                response[7] = 0xFFFF;
            }
            response_len = (char *)&response[8] - (char *)&response[0] + 0x256;
            data_len = response[8];
        }
        netbuf_handle = netbuf_handle;  /* Mark for cleanup */
        break;

    default:
        /* Most requests are handled by ASKNODE_$INTERNET_INFO */
        if (request.request_type == 2 || request.request_type == 4 ||
            request.request_type == 6 || request.request_type == 8 ||
            request.request_type == 0x0A || request.request_type == 0x0C ||
            request.request_type == 0x10 || request.request_type == 0x12 ||
            /* ... many more cases ... */
            request.request_type == 0x5B) {
            ASKNODE_$INTERNET_INFO((uint16_t *)&request.request_type,
                                   &NODE_$ME,
                                   (int32_t *)&DAT_00e658cc,
                                   (uid_t *)&request.node_id,
                                   (uint16_t *)&DAT_00e65e8c,
                                   (uint32_t *)&response[0],
                                   &status);
        } else {
            /* Unknown request type */
            response[1] = 0;
            response[7] = status_$network_unknown_request_type;
        }
        break;
    }

    /*
     * Send response (unless it was a time sync response which is handled specially)
     */
    if (request.request_type == 0x45) {
        status = 0;
    } else {
        /* Copy packet info block */
        {
            uint32_t *src = &DAT_00e82408;
            uint32_t *dst = pkt_info;
            int i;
            for (i = 0; i < 7; i++) *dst++ = *src++;
            *(uint16_t *)dst = *(uint16_t *)src;
        }
        *(uint16_t *)pkt_info = 0x20;
        pkt_info[2] = 1;  /* Response flag */

        PKT_$SEND_INTERNET(
            *(int32_t *)(pkt_data + 0x14),  /* routing */
            request.node_id,                 /* dest node */
            request_id,                      /* dest sock? */
            src_node,                        /* src info */
            src_node,                        /* src node */
            4,                               /* src sock */
            pkt_info,
            src_port,                        /* request ID */
            &response[0],                    /* response data */
            response_len,
            netbuf_va,                       /* data buffer */
            data_len,
            temp1,
            temp2,
            &status
        );
    }

    /* Clean up netbuf if allocated */
    if (netbuf_handle != 0) {
        uint32_t rtn = NETBUF_$RTNVA(&netbuf_va);
        NETBUF_$RTN_DAT(rtn);
    }

    /*
     * If propagation is needed and send succeeded, forward WHO query
     */
    if (should_propagate < 0 && status == 0) {
        if (request.request_type == 0x2D) {
            *(int8_t *)&request.count = 0;  /* Clear propagation flag */
        }
        response[0] = request.version;

        /* Copy response data */
        {
            uint32_t *src = (uint32_t *)&request.node_id;
            int16_t *dst = response + 2;
            int i;
            for (i = 0; i < 5; i++) {
                *(uint32_t *)dst = *src++;
                dst += 2;
            }
        }
        response[0xC] = src_port;
        response[0xD] = request_id;
    }
}
