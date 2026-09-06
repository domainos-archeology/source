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

/*
 * External references: NETWORK_$FAILURE_REC (0xE24BF4) from network/network.h,
 * ASKNODE_$EMPTY_DATA (0xE658CC) from asknode_internal.h.
 */

/* Response length word at 0x00E65E8C (PC-relative constant, value 0x200) */
static const uint16_t asknode_$server_resp_len = 0x200;

/*
 * The reply ASKNODE_$SERVER transmits is a local record at A6-0x250 with
 * asknode_response_t as its fixed head followed by up to 0x200 bytes of
 * request-specific body:
 *
 *   -0x250 +0x00 word  version           (0x00E65A30 / 0x00E65A36)
 *   -0x24E +0x02 word  response type     (0x00E65A24)
 *   -0x24C +0x04 long  status            (0x00E65B50, 0x00E65B6E, ...)
 *   -0x248 +0x08 long  responding node   (0x00E65B20)
 *   -0x244 +0x0C word  flags 0xB1FF      (0x00E65B3E)
 *   -0x242 +0x0E word  count remaining   (0x00E65B28)
 *
 * The `ctx` parameter (A2) is a different record entirely - see
 * asknode_$server_ctx_t.
 *
 * TODO(source-ai1l): the two records are now right, but the request dispatch
 * (the 0x5C-entry jump table at 0x00E65A60), the several "pkt_data + n" reads
 * that should come from the APP_$RECEIVE record at A6-0x30 or from locals,
 * and the argument shape of the PKT_$SEND_INTERNET call at 0x00E65E26 still
 * need re-deriving.
 */
#define ASKNODE_$REPLY_BODY_MAX     0x200
#define ASKNODE_$REPLY_FLAGS        0xB1FF

typedef struct asknode_$reply_t {
    asknode_response_t  hdr;                            /* 0x00 */
    uint8_t             body[ASKNODE_$REPLY_BODY_MAX];  /* 0x10 */
} asknode_$reply_t;

void ASKNODE_$SERVER(asknode_$server_ctx_t *ctx, int32_t *routing_info)
{
    asknode_$reply_t reply;
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

    /* 0x00E65A1E - 0x00E65A36 */
    reply.hdr.response_type = request.request_type + 1;
    if (request.version == 2) {
        reply.hdr.version = 2;
    } else {
        reply.hdr.version = 3;
    }

    /* 0x00E65A3C: D7 is the zero register the cases below store as status */
    reply.hdr.status = status_$ok;

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

            reply.hdr.node_id = NODE_$ME;                   /* 0x00E65B20 */
            reply.hdr.count = request.count;                /* 0x00E65B28 */
            request.count--;                                /* 0x00E65B32 */
            ctx->clock_lo = 0x1000;                         /* 0x00E65B36 */
            reply.hdr.flags = ASKNODE_$REPLY_FLAGS;         /* 0x00E65B3E */

            /* Determine routing */
            if (src_node == 0) {
                *routing_info = *(int32_t *)(pkt_data + 0x14);
            } else {
                *routing_info = src_node;
            }

            /* Check network capability (0x00E65B50 - 0x00E65B7E) */
            reply.hdr.status = status_$ok;
            {
                int16_t cap = ROUTE_$VALIDATE_PORT(*routing_info, true);
                if (cap == 2) {
                    reply.hdr.status = status_$network_operation_not_defined_on_hardware;
                } else if (cap == 0) {
                    reply.hdr.status = status_$network_unknown_network;
                }
            }

            /* Set propagation flag (0x00E65B80 - 0x00E65B9E) */
            should_propagate = (reply.hdr.status == status_$ok) &&
                               (request.count > 0) &&
                               (request.node_id != NODE_$ME) &&
                               ((flags & 4) == 0) ? -1 : 0;
            ctx->request_type = 0;                          /* 0x00E65BA0 */
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
            /* High byte of the count word (tst.b on the first byte of +0x10) */
            if ((int8_t)(request.count >> 8) < 0 || request.node_id != NODE_$ME) {
                reply.hdr.response_type = 0x2E;             /* 0x00E65BD0 */
                reply.hdr.node_id = NODE_$ME;               /* 0x00E65BD6 */
            } else {
                reply.hdr.node_id = request.param1;         /* 0x00E65BC2 */
                reply.hdr.response_type = 1;                /* 0x00E65BC8 */
            }

            reply.hdr.flags = ASKNODE_$REPLY_FLAGS;         /* 0x00E65BDE */
            if (src_node == 0) {
                *routing_info = *(int32_t *)(pkt_data + 0x14);
            } else {
                *routing_info = src_node;
            }

            reply.hdr.status = status_$ok;                  /* 0x00E65C02 */
            {
                int8_t is_local = (src_node == 0 || src_node == (int32_t)NODE_$ME) ? -1 : 0;
                int16_t cap = ROUTE_$VALIDATE_PORT(*routing_info, is_local);
                if (cap == 2) {
                    reply.hdr.status = status_$network_operation_not_defined_on_hardware;
                } else if (cap == 0) {
                    reply.hdr.status = status_$network_unknown_network;
                }
            }

            reply.hdr.count = request.count;                /* 0x00E65C32 */
            *(int32_t *)(pkt_data + 0x14) = request.param2;  /* 0x00E65C3C */
            request.count--;                                /* 0x00E65C42 */
            ctx->clock_lo = request.param3;                 /* 0x00E65C46 */

            /* Set propagation flag (0x00E65C4C - 0x00E65C72) */
            should_propagate = (reply.hdr.status == status_$ok) &&
                               (request.count > 0) &&
                               ((int8_t)(request.count >> 8) || request.node_id != NODE_$ME) &&
                               ((flags & 4) == 0) ? -1 : 0;
            ctx->request_type = 0x2D;                       /* 0x00E65C74 */
        }
        break;

    case 0x45:
        /* Time sync WHO query (0x00E65C7E - 0x00E65CD6) */
        reply.hdr.response_type = 0x46;
        reply.hdr.node_id = NODE_$ME;
        reply.hdr.status = status_$ok;
        reply.hdr.flags = ASKNODE_$REPLY_FLAGS;

        if (src_node == 0) {
            *routing_info = *(int32_t *)(pkt_data + 0x14);
        } else {
            *routing_info = src_node;
        }

        /*
         * The clock lands in the caller's context record, not in the reply:
         *   00e65ca2  pea (0x1c,A2) / jsr TIME_$CLOCK
         *   00e65cae  clr.w (0x1c,A2)
         *   00e65cb2  move.l (0x1e,A2),D0 / andi.l #0x7fffffff,D0
         *   00e65cbc  M$OIS$LLL(D0, request.param3) -> (0x1e,A2)
         */
        /* &ctx->clock_hi, spelled without taking the address of a packed
         * member (the field is at ctx+0x1C, which is longword aligned). */
        TIME_$CLOCK((clock_t *)((uint8_t *)ctx + 0x1C));
        ctx->clock_hi = 0;
        ctx->clock_lo = (uint32_t)M$OIS$LLL(ctx->clock_lo & 0x7FFFFFFF,
                                            request.param3);

        should_propagate = -1;                              /* 0x00E65CCE */
        ctx->request_type = 0x46;                           /* 0x00E65CD0 */
        break;

    case 0x0E:
        /* Record network failure */
        NETWORK_$FAILURE_REC.flag = 0xFF;
        NETWORK_$FAILURE_REC.error_info = request.param2;
        NETWORK_$FAILURE_REC.timestamp = TIME_$CURRENT_CLOCKH;
        NETWORK_$FAILURE_REC.node_id = request.node_id;
        return;  /* No response needed */

    case 0x31:
        /* Log read request */
        NETBUF_$GET_DAT(&netbuf_handle);
        NETBUF_$GETVA(netbuf_handle, &netbuf_va, &status);
        reply.hdr.status = status;                          /* 0x00E65D50 */

        if (status == 0) {
            if ((request.node_id & 0x10000) == 0) {
                uint16_t log_len = request.node_id;
                if (log_len > 0x400) log_len = 0x400;
                /*
                 *   00e65d8c    pea (-0x248,A6)      ; &actual_len
                 *   00e65d90    pea (-0x292,A6)      ; &log_len
                 *   00e65d94    pea (A3)             ; netbuf_va
                 */
                LOG_$READ(netbuf_va, &log_len, (uint16_t *)&reply.hdr.node_id);
            } else {
                /*
                 *   00e65d5e    pea (-0x248,A6)      ; &actual_len
                 *   00e65d62    move.w #0x400,-(SP)  ; max_len
                 *   00e65d66    move.w (-0x262,A6)   ; offset (high word of node_id)
                 *   00e65d6a    pea (A3)             ; netbuf_va
                 */
                LOG_$READ2(netbuf_va, (uint16_t)(request.node_id >> 16),
                           0x400, (uint16_t *)&reply.hdr.node_id);
                /*
                 * 0x00E65D76: "move.w #-1,(-0x24a,A6)" only writes the low
                 * half of the status longword.
                 */
                reply.hdr.status = (reply.hdr.status & 0xFFFF0000u) | 0xFFFFu;
            }
            /*
             * 0x00E65DA0 - 0x00E65DB0: the template is the reply head up to
             * (but not including) +0x0A, and the payload length is the word
             * LOG_$READ left at +0x08.
             */
            response_len = (uint16_t)(offsetof(asknode_response_t, flags) - 2);
            data_len = *(uint16_t *)&reply.hdr.node_id;
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
                                   (int32_t *)&ASKNODE_$EMPTY_DATA,
                                   (uid_t *)&request.node_id,
                                   (uint16_t *)&asknode_$server_resp_len,
                                   (uint32_t *)&reply,
                                   &status);
        } else {
            /* Unknown request type (0x00E65DB8 - 0x00E65DBC) */
            reply.hdr.response_type = 0;
            reply.hdr.status = status_$network_unknown_request_type;
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
            uint32_t *src = PKT_$DEFAULT_INFO;
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
            &reply,                          /* response template */
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
            request.count &= 0x00FF;  /* Clear propagation flag (high byte of the count word) */
        }
        /*
         * 0x00E65E60 - 0x00E65E7C: hand the caller the request's version,
         * its 20 bytes from +0x04, and the source port / request id so that
         * it can re-issue the query.
         */
        ctx->version = request.version;
        {
            const uint32_t *src = (const uint32_t *)&request.node_id;
            /* &ctx->node_id, spelled without taking the address of a packed
             * member (the field is at ctx+0x04, longword aligned). */
            uint32_t *dst = (uint32_t *)((uint8_t *)ctx + 0x04);
            int i;
            for (i = 0; i < 5; i++) {
                dst[i] = src[i];
            }
        }
        ctx->src_port = src_port;
        ctx->request_id = request_id;
    }
}
