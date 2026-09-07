/*
 * ASKNODE_$SERVER - Handle incoming node query requests
 *
 * Receives a request packet on socket 4, dispatches on the request type
 * through a 92-entry jump table, and (for every type but 0x0E and 0x45)
 * transmits a reply with PKT_$SEND_INTERNET.
 *
 * The dispatch is "cmpi.w #0x5c,D0w / bcc default / add.w D0w,D0w /
 * move.w (0xe65a60,PC,D0w*0x1),D0w / jmp (0xe65a60,PC,D0w*0x1)" at
 * 0x00E65A4A-0x00E65A5C.  The table at 0x00E65A60 holds 0x5C word offsets
 * from its own base; five request types have bodies of their own and a fixed
 * set delegates to ASKNODE_$INTERNET_INFO, everything else falls into the
 * "unknown request type" arm.  The compare is unsigned, so any value at or
 * above 0x5C - including a negative word - takes the default arm.
 *
 * A5 in the original is 0x00E82408, the PKT_$DEFAULT_INFO template.
 *
 * Original address: 0x00E6597A (1114 bytes)
 */

#include "asknode/asknode_internal.h"

/*
 * External references: NETWORK_$FAILURE_REC (0xE24BF4) and
 * TIME_$CURRENT_CLOCKH (0xE2B0E4) from network/network.h,
 * ASKNODE_$EMPTY_DATA (0xE658CC) from asknode_internal.h.
 */

/*
 * PC-relative constant cell of the original: the word 0x0200 at 0x00E65E8C,
 * passed to ASKNODE_$INTERNET_INFO as its response-length limit
 * ("pea (0x1a8,PC)" at 0x00E65CE2, whose target is 0x00E65CE4 + 0x1A8).
 */
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
 */
#define ASKNODE_$REPLY_BODY_MAX     0x200
#define ASKNODE_$REPLY_FLAGS        0xB1FF

typedef struct asknode_$reply_t {
    asknode_response_t  hdr;                            /* 0x00 */
    uint8_t             body[ASKNODE_$REPLY_BODY_MAX];  /* 0x10 */
} asknode_$reply_t;

/*
 * The five request types with a body of their own in the jump table are
 * ASKNODE_REQ_WHO (0x00 -> 0x00E65B18), ASKNODE_REQ_RECORD_FAILURE
 * (0x0E -> 0x00E65D04), ASKNODE_REQ_WHO_REMOTE (0x2D -> 0x00E65BA8),
 * ASKNODE_REQ_LOG_READ (0x31 -> 0x00E65D24) and ASKNODE_REQ_TIME_SYNC
 * (0x45 -> 0x00E65C7E); all are declared in asknode/asknode.h.
 */

/* The reply type each of those answers with */
#define ASKNODE_RSP_WHO_REMOTE      0x2E
#define ASKNODE_RSP_TIME_SYNC       0x46

/* The socket ASKNODE_$SERVER listens on and answers from */
#define ASKNODE_SERVER_SOCKET       4

/*
 * asknode_$server_delegates - the request types whose jump-table entry is the
 * shared ASKNODE_$INTERNET_INFO arm at 0x00E65CDA (offset 0x027A).  Read
 * directly out of the table at 0x00E65A60; every other index below 0x5C
 * carries offset 0x0358 (the "unknown request type" arm at 0x00E65DB8) apart
 * from the five with bodies of their own.
 */
static boolean asknode_$server_delegates(uint16_t request_type)
{
    switch (request_type) {
    case 0x02: case 0x04: case 0x06: case 0x08: case 0x0A: case 0x0C:
    case 0x10: case 0x12: case 0x14: case 0x16: case 0x18: case 0x1A:
    case 0x1C: case 0x21: case 0x23: case 0x25: case 0x27: case 0x29:
    case 0x2B: case 0x2F: case 0x33: case 0x35: case 0x37: case 0x39:
    case 0x3B: case 0x3D: case 0x3F: case 0x41: case 0x43: case 0x47:
    case 0x49: case 0x4B: case 0x4D: case 0x4F: case 0x51: case 0x55:
    case 0x57: case 0x59: case 0x5B:
        return (boolean)-1;
    default:
        return (boolean)0;
    }
}

void ASKNODE_$SERVER(asknode_$server_ctx_t *ctx, int32_t *routing_info)
{
    app_$receive_rec_t  rcv;                /* A6-0x30 */
    uint32_t            pkt_info[8];        /* A6-0x50, 30 bytes are copied */
    asknode_$reply_t    reply;              /* A6-0x250 */
    asknode_request_t   request;            /* A6-0x268 */
    uint32_t            log_va;             /* A6-0x270 */
    status_$t           log_status;         /* A6-0x26C */
    uint32_t            log_ppn;            /* A6-0x274 */
    status_$t           status;             /* A6-0x278 */
    uint32_t            routing_key;        /* A6-0x27C */
    uint16_t            retry_hint;         /* A6-0x28C */
    uint16_t            timeout_out;        /* A6-0x28A */
    uint16_t            log_len;            /* A6-0x292 */
    uint16_t            template_len;       /* A6-0x294 */
    uint16_t            data_len;           /* A6-0x296 */
    int16_t             request_id;         /* A6-0x29A */
    uint16_t            src_socket;         /* A6-0x29C */
    int8_t              is_local;           /* A6-0x2A2 */

    asknode_$reply_hdr_t *rx;               /* A0, = rcv.reply */
    int32_t             src_node_or;        /* D3 */
    uint16_t            rx_flags;           /* D4 */
    uint32_t            sender_node;        /* D5 */
    uint32_t            dest_node;          /* D6 */
    uint32_t            dat_handle;         /* D7 */
    int8_t              should_propagate;   /* D2 */
    uint16_t            copy_len;           /* D2, before it is reused */
    void               *send_data;          /* A3 */

    /* 0x00E65990: receive on socket 4 into the 44-byte record at A6-0x30 */
    APP_$RECEIVE(ASKNODE_SERVER_SOCKET, &rcv, &status);
    if (status != 0) {                                  /* 0x00E659A8 */
        return;
    }

    rx = (asknode_$reply_hdr_t *)ARCH_VA_TO_PTR(rcv.reply);

    /* 0x00E659B0: hand back the payload pages straight away */
    PKT_$DUMP_DATA(rcv.data_pages, rx->data_len);

    /* 0x00E659C6 */
    src_node_or = (int32_t)rcv.hdr_f06;
    routing_key = rcv.hdr_f12;

    /* 0x00E659D0-0x00E659E8: everything the received header is read for */
    sender_node = rx->sender_node;
    dest_node   = rx->node_id;
    src_socket  = rx->src_socket;
    rx_flags    = rx->f14;
    request_id  = rx->reply_id;

    /* 0x00E659EE: copy at most 0x18 bytes of request out of the payload */
    copy_len = 0x18;
    if (rx->length < copy_len) {
        copy_len = rx->length;
    }
    OS_$DATA_COPY((char *)ARCH_VA_TO_PTR(rcv.data), (char *)&request,
                  (int32_t)copy_len);

    /* 0x00E65A12: the header buffer goes back once everything is copied */
    NETBUF_$RTN_HDR(&rcv.data);

    /* 0x00E65A1E - 0x00E65A36 */
    reply.hdr.response_type = (uint16_t)(request.request_type + 1);
    if (request.version == 2) {
        reply.hdr.version = 2;
    } else {
        reply.hdr.version = 3;
    }

    /* 0x00E65A3C - 0x00E65A48 */
    dat_handle       = 0;
    data_len         = 0;
    template_len     = ASKNODE_$REPLY_BODY_MAX;
    should_propagate = 0;
    send_data        = routing_info;    /* A3 still holds the parameter */

    /* 0x00E65A4A: dispatch */
    if (request.request_type == ASKNODE_REQ_WHO) {
        /* ---- 0x00E65B18 ---- */

        /*
         * 0x00E65B18: bit 7 of the receive record's +0x27 says the packet
         * came in on a path this server must not answer.
         */
        if ((int8_t)(rcv.flags_hi & 0xFF) < 0) {
            return;
        }

        reply.hdr.node_id = NODE_$ME;                       /* 0x00E65B20 */

        /*
         * 0x00E65B28 / 0x00E65B32: the hop counter is the HIGH word of the
         * longword at request+0x08.
         */
        reply.hdr.count = (int16_t)(request.param1 >> 16);
        dest_node = request.node_id;                        /* 0x00E65B2E */
        request.param1 = (request.param1 & 0x0000FFFFu) |
                         ((uint32_t)(uint16_t)(reply.hdr.count - 1) << 16);

        ctx->clock_lo   = 0x1000;                           /* 0x00E65B36 */
        reply.hdr.flags = ASKNODE_$REPLY_FLAGS;             /* 0x00E65B3E */

        /* 0x00E65B44 */
        if (src_node_or == 0) {
            *routing_info = (int32_t)routing_key;
        } else {
            *routing_info = src_node_or;
        }

        /* 0x00E65B50 - 0x00E65B7E */
        reply.hdr.status = status_$ok;
        {
            int16_t cap = ROUTE_$VALIDATE_PORT(*routing_info, (int8_t)0xFF);
            if (cap == 2) {
                reply.hdr.status = status_$network_operation_not_defined_on_hardware;
            } else if (cap == 0) {
                reply.hdr.status = status_$network_unknown_network;
            }
        }

        /* 0x00E65B80 - 0x00E65B9E */
        should_propagate =
            (reply.hdr.status == status_$ok) &&
            ((int16_t)(request.param1 >> 16) > 0) &&
            (dest_node != NODE_$ME) &&
            ((rx_flags & 0x0004) == 0) ? (int8_t)0xFF : 0;

        ctx->request_type = 0;                              /* 0x00E65BA0 */

    } else if (request.request_type == ASKNODE_REQ_WHO_REMOTE) {
        /* ---- 0x00E65BA8 ---- */

        if ((int8_t)(rcv.flags_hi & 0xFF) < 0) {            /* 0x00E65BA8 */
            return;
        }

        /* 0x00E65BB0 - 0x00E65BDC */
        if (request.forwarded < 0 || request.node_id != NODE_$ME) {
            reply.hdr.response_type = ASKNODE_RSP_WHO_REMOTE;
            reply.hdr.node_id       = NODE_$ME;
        } else {
            reply.hdr.node_id       = request.param1;
            reply.hdr.response_type = 1;
        }

        reply.hdr.flags = ASKNODE_$REPLY_FLAGS;             /* 0x00E65BDE */

        /* 0x00E65BE4 */
        if (src_node_or == 0) {
            *routing_info = (int32_t)routing_key;
        } else {
            *routing_info = src_node_or;
        }

        /* 0x00E65BF0 */
        is_local = (sender_node == 0 || sender_node == NODE_$ME)
                       ? (int8_t)0xFF : 0;

        /* 0x00E65C02 - 0x00E65C30 */
        reply.hdr.status = status_$ok;
        {
            int16_t cap = ROUTE_$VALIDATE_PORT(*routing_info, is_local);
            if (cap == 2) {
                reply.hdr.status = status_$network_operation_not_defined_on_hardware;
            } else if (cap == 0) {
                reply.hdr.status = status_$network_unknown_network;
            }
        }

        reply.hdr.count = request.count;                    /* 0x00E65C32 */
        dest_node       = request.param1;                   /* 0x00E65C38 */
        routing_key     = request.param2;                   /* 0x00E65C3C */
        request.count--;                                    /* 0x00E65C42 */
        ctx->clock_lo   = request.param3;                   /* 0x00E65C46 */

        /* 0x00E65C4C - 0x00E65C72 */
        should_propagate =
            (reply.hdr.status == status_$ok) &&
            (request.count > 0) &&
            ((request.node_id != NODE_$ME) || (request.forwarded != 0)) &&
            ((rx_flags & 0x0004) == 0) ? (int8_t)0xFF : 0;

        ctx->request_type = ASKNODE_REQ_WHO_REMOTE;         /* 0x00E65C74 */

    } else if (request.request_type == ASKNODE_REQ_TIME_SYNC) {
        /* ---- 0x00E65C7E ---- */

        reply.hdr.response_type = ASKNODE_RSP_TIME_SYNC;
        reply.hdr.node_id       = NODE_$ME;
        reply.hdr.status        = status_$ok;
        reply.hdr.flags         = ASKNODE_$REPLY_FLAGS;

        /* 0x00E65C96 */
        if (src_node_or == 0) {
            *routing_info = (int32_t)routing_key;
        } else {
            *routing_info = src_node_or;
        }

        /*
         * The clock lands in the caller's context record, not in the reply:
         *   00e65ca2  pea (0x1c,A2) / jsr TIME_$CLOCK
         *   00e65cae  clr.w (0x1c,A2)
         *   00e65cb2  move.l (0x1e,A2),D0 / andi.l #0x7fffffff,D0
         *   00e65cbc  M$OIS$LLL(D0, request.param3) -> (0x1e,A2)
         * ctx+0x1C is longword aligned, so the address is taken without
         * touching a packed member.
         */
        TIME_$CLOCK((clock_t *)((uint8_t *)ctx + 0x1C));
        ctx->clock_hi = 0;
        ctx->clock_lo = (uint32_t)M$OIS$LLL(ctx->clock_lo & 0x7FFFFFFFu,
                                            request.param3);

        should_propagate  = (int8_t)0xFF;                   /* 0x00E65CCE */
        ctx->request_type = ASKNODE_RSP_TIME_SYNC;          /* 0x00E65CD0 */

    } else if (request.request_type == ASKNODE_REQ_RECORD_FAILURE) {
        /* ---- 0x00E65D04: record the failure and answer nothing ---- */
        NETWORK_$FAILURE_REC.flag       = (int8_t)0xFF;     /* 0x00E65D0A */
        NETWORK_$FAILURE_REC.node_id      = dest_node;      /* 0x00E65D0E */
        NETWORK_$FAILURE_REC.timestamp    = TIME_$CURRENT_CLOCKH;
        NETWORK_$FAILURE_REC.failure_type = request.node_id;/* 0x00E65D1A */
        return;                                             /* 0x00E65D20 */

    } else if (request.request_type == ASKNODE_REQ_LOG_READ) {
        /* ---- 0x00E65D24 ---- */

        NETBUF_$GET_DAT(&log_ppn);                          /* 0x00E65D24 */
        dat_handle = log_ppn;                               /* 0x00E65D30 */
        NETBUF_$GETVA(dat_handle, &log_va, &log_status);    /* 0x00E65D34 */

        /*
         * 0x00E65D48: A3 - which held routing_info - is reloaded with the
         * data buffer's virtual address and stays that way for the send.
         */
        send_data = (void *)(uintptr_t)log_va;

        reply.hdr.status = log_status;                      /* 0x00E65D50 */
        if (log_status == 0) {
            /*
             * 0x00E65D56: "btst.l D2,D1" with D2 still zero (cleared at
             * 0x00E65A48, and its upper bits are zero because copy_len never
             * exceeds 0x18) - so this is bit 0 of the HIGH word of
             * request+0x04.
             */
            if ((request.node_id & 0x00010000u) != 0) {
                /*
                 *   00e65d5e    pea (-0x248,A6)      ; &reply.hdr.node_id
                 *   00e65d62    move.w #0x400,-(SP)  ; max_len
                 *   00e65d66    move.w (-0x262,A6)   ; offset = LOW word of +0x04
                 *   00e65d6a    pea (A3)             ; the data buffer
                 */
                LOG_$READ2(send_data, (uint16_t)request.node_id, 0x400,
                           (uint16_t *)&reply.hdr.node_id);
                /*
                 * 0x00E65D76 "move.w #-1,(-0x24a,A6)" writes only the LOW
                 * half of the status longword at reply+0x04.
                 */
                reply.hdr.status = (status_$t)
                    ((uint32_t)reply.hdr.status & 0xFFFF0000u) | 0xFFFFu;
            } else {
                /* 0x00E65D7E: clamp the HIGH word of +0x04 to 0x400 */
                log_len = (uint16_t)(request.node_id >> 16);
                if (log_len > 0x400) {
                    log_len = 0x400;
                }
                LOG_$READ(send_data, &log_len,
                          (uint16_t *)&reply.hdr.node_id);
            }

            /*
             * 0x00E65DA0 - 0x00E65DB0: the transmitted template is the reply
             * head up to +0x0A ("lea (-0x250,A6),A0 / lea (-0x246,A6),A1 /
             * sub.l A0,D1"), and the payload byte count is the word
             * LOG_$READ left at reply+0x08.
             */
            template_len = 0x0A;
            data_len     = *(uint16_t *)&reply.hdr.node_id;
        }

    } else if (asknode_$server_delegates(request.request_type)) {
        /* ---- 0x00E65CDA ---- */
        ASKNODE_$INTERNET_INFO(&request.request_type,
                               &NODE_$ME,
                               (int32_t *)&ASKNODE_$EMPTY_DATA,
                               (uid_t *)&request.node_id,
                               (uint16_t *)&asknode_$server_resp_len,
                               (uint32_t *)&reply,
                               &status);

    } else {
        /* ---- 0x00E65DB8 ---- */
        reply.hdr.response_type = 0;
        reply.hdr.status = status_$network_unknown_request_type;
    }

    /* ---- 0x00E65DC4: send the reply ---- */
    if (request.request_type == ASKNODE_REQ_TIME_SYNC) {
        /* 0x00E65DCC: the time-sync answer travels in the context record */
        status = status_$ok;
    } else {
        /* 0x00E65DD2: copy the 30-byte PKT_$DEFAULT_INFO template */
        {
            uint32_t *src = PKT_$DEFAULT_INFO;
            uint32_t *dst = pkt_info;
            int i;
            for (i = 0; i < 7; i++) {
                *dst++ = *src++;
            }
            *(uint16_t *)dst = *(uint16_t *)src;
        }
        *(uint16_t *)pkt_info       = 0x20;                 /* 0x00E65DE4 */
        *((uint16_t *)pkt_info + 4) = 1;                    /* 0x00E65DEA */

        /*
         * 0x00E65DF0 - 0x00E65E2C.  The pushes, in argument order:
         *   1  (-0x27c,A6)  2  D6           3  (-0x29c,A6) word
         *   4  D3           5  D5           6  #4 word
         *   7  &(-0x50,A6)  8  (-0x29a,A6) word
         *   9  &(-0x250,A6) 10 (-0x294,A6) word
         *   11 A3           12 (-0x296,A6) word
         *   13 &(-0x28c,A6) 14 &(-0x28a,A6) 15 &(-0x278,A6)
         * plus the 2-byte Pascal function-result slot; the caller pops 0x34.
         *
         * Note that argument 11 is whatever A3 holds: the caller's
         * routing_info pointer on every path but request 0x31, where the
         * log data buffer replaced it.  data_len is zero on those paths, so
         * PKT_$SEND_INTERNET never dereferences it (0x00E12686).
         */
        PKT_$SEND_INTERNET(routing_key,             /* 1  routing_key   */
                           dest_node,               /* 2  dest_node     */
                           src_socket,              /* 3  dest_sock     */
                           src_node_or,             /* 4  src_node_or   */
                           sender_node,             /* 5  src_node      */
                           ASKNODE_SERVER_SOCKET,   /* 6  src_sock      */
                           pkt_info,                /* 7  pkt_info      */
                           (uint16_t)request_id,    /* 8  request_id    */
                           &reply,                  /* 9  template      */
                           template_len,            /* 10 template_len  */
                           send_data,               /* 11 data          */
                           (int16_t)data_len,       /* 12 data_len      */
                           &retry_hint,             /* 13 retry_hint    */
                           &timeout_out,            /* 14 timeout_out   */
                           &status);                /* 15 status_ret    */
    }

    /* 0x00E65E30: give the log data buffer back */
    if (dat_handle != 0) {
        uint32_t ppn = NETBUF_$RTNVA(&log_va);
        NETBUF_$RTN_DAT(ppn);
    }

    /*
     * 0x00E65E4A - 0x00E65E7C: when the request must be propagated further,
     * fill in the caller's context record so it can forward the query.
     */
    if (should_propagate < 0 && status == 0) {
        if (request.request_type == ASKNODE_REQ_WHO_REMOTE) {
            request.forwarded = 0;                          /* 0x00E65E5C */
        }

        ctx->version = request.version;                     /* 0x00E65E60 */

        /*
         * 0x00E65E64: five longwords, request+0x04 .. +0x17.  Copied field by
         * field because asknode_$server_ctx_t is packed and its members must
         * not have their addresses taken.
         */
        ctx->node_id   = request.node_id;
        ctx->param1    = request.param1;
        ctx->param2    = request.param2;
        ctx->forwarded = request.forwarded;
        ctx->_pad_11   = request._pad_11;
        ctx->count     = request.count;
        ctx->param3    = request.param3;

        ctx->request_id = request_id;                       /* 0x00E65E76 */
        ctx->socket     = src_socket;                       /* 0x00E65E7C */
    }
}
