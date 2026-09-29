/*
 * PKT_$PING_SERVER - the ping-service process
 *
 * Opens socket 0x0D, takes the "network server" lock and then loops forever:
 * wait on the socket's event count, receive one packet, hand its header and
 * data pages back to the buffer pools, and bounce a 2-byte reply to the
 * sender with the request flags rewritten as a reply.
 *
 * The routine never returns; the only exits are CRASH_SYSTEM on a failed
 * socket open and the unconditional "bra.w" back to the wait.
 *
 * Original address: 0x00E12BB8 (378 bytes)
 * Module base: A5 = 0x00E24C9C = PKT_$DATA (0x00E12BC0 "lea (0xe24c9c).l,A5")
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

/*
 * CRASH_SYSTEM takes its status by reference; the original passes a constant
 * cell in the code region: 0x00E12BE4 "pea (0x14e,PC)" resolves to
 * 0x00E12BE4 + 2 + 0x14E = 0x00E12D34, which holds 0x0011000C
 * ("no more free sockets").
 */
static status_$t pkt_$sock_open_failed = status_$network_no_more_free_sockets;

/*
 * 0x00E12BF2 "move.w #0x13,-(SP)" - PROC1_$SET_LOCK's lock id.
 */
#define PKT_PING_SERVER_LOCK 0x13

/*
 * 0x00E12BC8 / 0x00E12BCC - SOCK_$OPEN's buffer-page and queue arguments.
 */
#define PKT_PING_SOCK_BUFPAGES 0x00030000

void PKT_$PING_SERVER(void)
{
    boolean   opened;                   /* D0.b from SOCK_$OPEN */
    int32_t   wait_val;                 /* D2 */
    uint16_t  dest_sock;                /* A6-0x64: the requester's socket */
    int16_t   request_id;               /* A6-0x62 */
    char      template_buf[4];          /* A6-0x60 */
    uint16_t  data_len;                 /* A6-0x5C */
    uint16_t  retry_hint;               /* A6-0x5A */
    uint16_t  resp_timeout;             /* A6-0x58 */
    uint32_t  routing_key;              /* A6-0x50 */
    uint32_t  dest_node;                /* A6-0x4C */
    status_$t status;                   /* A6-0x48 */
    uint32_t  hdr_page;                 /* A6-0x44 */
    app_$receive_rec_t recv;            /* A6-0x30 (the record APP_$RECEIVE fills) */
    pkt_$internet_hdr_t *req_hdr;       /* A6-0x30 +0x00, "movea.l (-0x30,A6),A0" */
    uint16_t  tpl_len;                  /* D3 */
    uint16_t  reply_flags;              /* D4 */

    /*
     * 0x00E12BC6 - 0x00E12BDC: a word result slot then, right to left,
     * 0x400, 0x30000, socket 0x0D.
     * 0x00E12BE0 "tst.b D0b / bmi" - a Domain boolean, true (< 0) means the
     * socket was opened.
     */
    opened = SOCK_$OPEN(PKT_PING_SOCKET, PKT_PING_SOCK_BUFPAGES, PKT_CHUNK_SIZE);
    if (opened >= 0) {
        CRASH_SYSTEM(&pkt_$sock_open_failed);   /* 0x00E12BE8 */
    }

    PROC1_$SET_LOCK(PKT_PING_SERVER_LOCK);      /* 0x00E12BF6 */

    /*
     * 0x00E12BFE  movea.l (0x00e28de4).l,A0
     * 0x00E12C04  move.l (A0),D2
     * 0x00E12C0C  addq.l #0x1,D2
     * 0xE28DE4 == 0xE28DB4 + (0x0D - 1) * 4, i.e.
     * SOCK_$DATA.socket_ptr[PKT_PING_SOCKET].
     */
    wait_val = SOCK_$DATA.socket_ptr[PKT_PING_SOCKET]->ec.value + 1;

    for (;;) {
        /*
         * 0x00E12C28 - 0x00E12C42.  Two 3-element arrays by value (24 bytes),
         * pushed right to left:
         *   0x00E12C28  pea (0x1).w              vals[2] = 1
         *   0x00E12C2C  move.l (SP),-(SP)        vals[1] = 1 (a copy of vals[2])
         *   0x00E12C2E  move.l D2,-(SP)          vals[0] = wait_val
         *   0x00E12C30  pea (A2)   A2 == 0       ecs[2] = NULL
         *   0x00E12C32  pea (A2)                 ecs[1] = NULL
         *   0x00E12C34  movea.l (0x00e28de4).l,A0
         *   0x00E12C3A  pea (A0)                 ecs[0] = the socket's ec
         * ecs[1] is NULL, so vals[1] and vals[2] are never looked at; they are
         * kept because the original still pushes them.  The result is
         * discarded (no result slot is reserved).
         */
        (void)EC_$WAIT((ec_$wait_ecs_t){{ &SOCK_$DATA.socket_ptr[PKT_PING_SOCKET]->ec,
                                          NULL, NULL }},
                       (ec_$wait_vals_t){{ wait_val, 1, 1 }});

        /* 0x00E12C46 - 0x00E12C5A: result slot, &status, &recv, socket. */
        APP_$RECEIVE(PKT_PING_SOCKET, &recv, &status);

        /*
         * 0x00E12C5E "tst.l (-0x48,A6)" / 0x00E12C62 "bne.b 0x00E12C28":
         * a failed receive goes back to the wait WITHOUT advancing wait_val.
         */
        if (status != status_$ok) {
            continue;
        }

        routing_key = recv.hdr_f12;             /* 0x00E12C64 */

        /* 0x00E12C6A "movea.l (-0x30,A6),A0" - the reply record APP_$RECEIVE
         * left at +0x00 is what PKT parses as the request header. */
        req_hdr     = (pkt_$internet_hdr_t *)ARCH_VA_TO_PTR(recv.reply);
        data_len    = req_hdr->data_len;        /* 0x00E12C6E "(0x4,A0)" */
        dest_node   = req_hdr->src_node;        /* 0x00E12C74 "(0xe,A0)" */
        dest_sock   = req_hdr->src_sock;        /* 0x00E12C7A "(0x12,A0)" */

        /* 0x00E12C80 "clr.w D4w" / 0x00E12C82 "move.b (0x14,A0),D4b" - the
         * flags byte is zero-extended into the low half of D4. */
        reply_flags = req_hdr->flags;

        request_id  = req_hdr->request_id;      /* 0x00E12C86 "(0x6,A0)" */

        /*
         * 0x00E12C8C  moveq #0x2,D3
         * 0x00E12C8E  cmp.w (0x2,A0),D3w / bls   keep 2 when 2 <= hdr_len
         * 0x00E12C94  move.w (0x2,A0),D3w
         */
        tpl_len = 2;
        if (tpl_len > req_hdr->hdr_len) {
            tpl_len = req_hdr->hdr_len;
        }

        /*
         * 0x00E12C98 - 0x00E12CAC: the request template is copied out of the
         * payload into a stack buffer whose address was cached at
         * 0x00E12C22 ("lea (-0x60,A6),A1 / move.l A1,(-0x6c,A6)").  Nothing
         * ever reads template_buf back - the reply template comes from
         * PKT_$DATA.ping_req_hdr - but the copy is part of the original.
         */
        OS_$DATA_COPY(ARCH_VA_TO_PTR(recv.data), template_buf,
                      (uint32_t)tpl_len);

        /*
         * 0x00E12CB0  move.l (-0x2c,A6),D0
         * 0x00E12CB4  andi.w #-0x400,D0w      a WORD and: only bits 0..9 die
         * 0x00E12CB8  move.l D0,(-0x44,A6)
         */
        hdr_page = recv.data & 0xFFFFFC00u;
        NETBUF_$RTN_HDR(&hdr_page);             /* 0x00E12CC0 */

        /* 0x00E12CC8 "tst.l (-0x28,A6)" - the first data page slot. */
        if (recv.data_pages[0] != 0) {
            PKT_$DUMP_DATA(recv.data_pages, (int16_t)data_len);  /* 0x00E12CD8 */
        }

        /*
         * 0x00E12CDE  andi.w #-0x91,D4w    clear bits 0x80 and 0x10
         * 0x00E12CE2  ori.w #0x20,D4w      mark the packet as a reply
         * 0x00E12CE6  move.w D4w,(0x88,A5)
         */
        reply_flags = (uint16_t)((reply_flags & 0xFF6F) | 0x20);
        PKT_$DATA.ping_reply_info.flags = reply_flags;

        /*
         * 0x00E12CEA - 0x00E12D28: fifteen arguments plus the word result
         * slot, popped with "lea (0x34,SP),SP".  Pushed right to left:
         *   0x00E12CEC  pea (-0x48,A6)          &status
         *   0x00E12CF0  pea (-0x58,A6)          &resp_timeout
         *   0x00E12CF4  pea (-0x5a,A6)          &retry_hint
         *   0x00E12CF8  clr.w -(SP)             data_len = 0
         *   0x00E12CFA  pea (-0x148,PC)         &pkt_$no_data
         *   0x00E12CFE  move.w #0x2,-(SP)       template_len = 2
         *   0x00E12D02  pea (0x5a,A5)           &PKT_$DATA.ping_req_hdr
         *   0x00E12D06  move.w (-0x62,A6),-(SP) request_id
         *   0x00E12D0A  pea (0x88,A5)           &PKT_$DATA.ping_reply_info
         *   0x00E12D0E  move.w #0xd,-(SP)       src_sock = the ping socket
         *   0x00E12D12  move.l (A3),-(SP)       NODE_$ME
         *   0x00E12D14  pea (-0x1).w            src_node_or = -1
         *   0x00E12D18  move.w (-0x64,A6),-(SP) dest_sock
         *   0x00E12D1C  move.l (-0x4c,A6),-(SP) dest_node
         *   0x00E12D20  move.l (-0x50,A6),-(SP) routing_key
         * The status word is the same local APP_$RECEIVE wrote; its value
         * after the send is never examined.
         */
        PKT_$SEND_INTERNET(routing_key, dest_node, dest_sock,
                           -1, NODE_$ME, PKT_PING_SOCKET,
                           &PKT_$DATA.ping_reply_info, (uint16_t)request_id,
                           &PKT_$DATA.ping_req_hdr, 2,
                           (void *)&pkt_$no_data, 0,
                           &retry_hint, &resp_timeout,
                           &status);

        wait_val++;                             /* 0x00E12D2C "addq.l #0x1,D2" */
    }
}
