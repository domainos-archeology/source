/*
 * MSG_$SEND, MSG_$SENDI, MSG_$$SEND - Send a message
 *
 * MSG_$$SEND is the shared body; the two wrappers only unpack their
 * by-reference arguments and hand the answer back.
 *
 * Original addresses:
 *   MSG_$SEND:  0x00E599FC (170 bytes)
 *   MSG_$SENDI: 0x00E59AA6 (110 bytes)
 *   MSG_$$SEND: 0x00E0D9EC (732 bytes)
 */

#include "msg/msg_internal.h"
#include "os/os.h"
#include "time/time.h"

/* Largest template MSG will build a header for (0x00E0DA16) */
#define MSG_MAX_TEMPLATE_LEN 0x200

/* Largest payload that fits the single bounce page (0x00E0DBF0) */
#define MSG_DPAGE_MAX_DATA 0x400

/* pkt_$info_t.flags bit MSG_$$SEND always adds (0x00E0DA38 bset.b #2,+0x01) */
#define MSG_INFO_FLAG_MSG 0x0004

/* net_io_$send_info_t.xmit_status bits MSG_$$SEND writes itself */
#define MSG_XMIT_LOCAL     0x0008  /* 0x00E0DB4E  bset.b #3,(0x3,A3) */
#define MSG_XMIT_QUEUED    0x8000  /* 0x00E0DBB2  the SOCK_$PUT boolean */

/*
 * MSG_$$SEND - build a packet header for the caller's message and either
 * queue it on a local socket or hand it to the network.
 *
 * Original address: 0x00E0D9EC
 */
void MSG_$$SEND(int16_t port_num, uint32_t routing_key, uint32_t dest_node,
                uint16_t dest_sock, int32_t src_node_or, uint32_t src_node,
                uint16_t src_sock, const pkt_$info_t *pkt_info,
                uint16_t request_id, void *template, uint16_t template_len,
                void *data, uint16_t data_len,
                net_io_$send_info_t *send_info, status_$t *status_ret)
{
    pkt_$info_t info;               /* A6-0x60, 30 of its 32 bytes copied */
    uint32_t hdr_pa;                /* A6-0x88, NETBUF_$GET_HDR's first out */
    uint32_t hdr_va;                /* A6-0x80, NETBUF_$GET_HDR's second out */
    pkt_$hdr_t *hdr;                /* D7 */
    status_$t local_status;         /* A6-0x8C */
    int16_t port;                   /* D5 (D5w is the template length first) */
    uint16_t hdr_len;               /* D6, A6-0x96 */
    uint16_t retry_hint;            /* A6-0x92 */
    uint16_t resp_timeout;          /* A6-0x90 */
    int16_t bld_port;               /* A6-0x98 */
    uint32_t data_pages[4];         /* A6-0x70 */
    uint32_t data_va;               /* A6-0x7C */
    uint16_t send_flags;            /* A6-0x94 */
    net_io_$send_info_t net_info;   /* A6-0x78 */
    sock_$pkt_info_t local_pkt;     /* A6-0x40 */
    int16_t i;

    /*
     * D3 holds the port argument until the remote path reuses it as the
     * "the bounce page is ours" boolean ("clr.b D3b" at 0x00E0DBE0).  The
     * cleanup at 0x00E0DCA2 tests it as a BYTE, and the early-error paths
     * reach that cleanup with D3 still holding the port number - so a
     * MSG_$SEND / MSG_$SENDI caller (port -1, D3b = 0xFF) that fails the
     * header build drops MSG_$DPAGE->in_use without ever having claimed it.
     * That is what the original does; it is reproduced here, not repaired.
     */
    int8_t used_dpage = (int8_t)port_num;

    data_pages[0] = 0;              /* 0x00E0DA12  clr.l (-0x70,A6) */

    /* 0x00E0DA16  cmpi.w #0x200,D5w / bls  (unsigned) */
    if (template_len > MSG_MAX_TEMPLATE_LEN) {
        *status_ret = status_$network_message_header_too_big;
        return;                     /* 0x00E0DA22, nothing to release yet */
    }

    /*
     * 0x00E0DA26 - 0x00E0DA36: a 30-byte copy (moveq #6 + dbf of longs, then
     * one word).  pkt_$info_t's trailing pad word is NOT copied, so it keeps
     * whatever the stack held; PKT_$BLD_INTERNET_HDR never reads it.
     */
    for (i = 0; i < 0x1E; i++) {
        ((uint8_t *)&info)[i] = ((const uint8_t *)pkt_info)[i];
    }

    /* 0x00E0DA38  bset.b #0x2,(-0x5f,A6) - bit 2 of the flags word's LOW byte */
    info.flags |= MSG_INFO_FLAG_MSG;

    /* 0x00E0DA3E  pea (-0x80,A6) / pea (-0x88,A6) */
    NETBUF_$GET_HDR(&hdr_pa, &hdr_va);
    hdr = (pkt_$hdr_t *)ARCH_VA_TO_PTR(hdr_va);     /* 0x00E0DA4E */

    /* 0x00E0DA56 - 0x00E0DA98 */
    PKT_$BLD_INTERNET_HDR(routing_key, dest_node, dest_sock, src_node_or,
                          src_node, src_sock, &info, request_id,
                          template, template_len, data_len,
                          &bld_port, hdr, &hdr_len,
                          &retry_hint, &resp_timeout, &local_status);

    /* 0x00E0DA9C / 0x00E0DAA0: D6 and D5 reload the builder's two out-words */
    port = bld_port;

    /*
     * 0x00E0DAA4  cmpi.w #-0x1,D3w
     * An explicit port overrides the one the header builder chose, and the
     * "unknown network" the builder reported for it is forgiven.
     */
    if (port_num != -1) {
        port = port_num;            /* 0x00E0DAAA */

        /* 0x00E0DAAC  cmpi.l #0x110017,(-0x8c,A6) */
        if (local_status == status_$network_unknown_network) {
            const route_$driver_info_t *drv;

            local_status = status_$ok;                  /* 0x00E0DAB6 */
            drv = (const route_$driver_info_t *)
                      ARCH_VA_TO_PTR(ROUTE_$PORTP[port]->driver_info);
                                                        /* 0x00E0DABA-0x00E0DACC */

            /* 0x00E0DAD0  cmp.w (0x2,A0),D2w / bls  (unsigned) */
            if (data_len > drv->max_data_len) {
                local_status = status_$network_data_length_too_large;
            } else if ((int32_t)((uint32_t)data_len + (int32_t)(int16_t)hdr_len) >
                       (int32_t)((uint32_t)drv->max_data_len + 0x100)) {
                /*
                 * 0x00E0DAE0 - 0x00E0DAFA.  The header length is SIGN
                 * extended ("ext.l D1") while the data length is zero
                 * extended, and the compare is signed ("cmp.l D1,D0 / ble").
                 */
                local_status = status_$network_msg_exceeds_max_size;
            }
        }
    }

    /* 0x00E0DB02  tst.l (-0x8c,A6) / bne 0x00e0dc96 */
    if (local_status != status_$ok) {
        goto release;
    }

    /*
     * 0x00E0DB0A - 0x00E0DB20: the port's own network number, and a cleared
     * transmit status.  This is net_io_$send_info_t, the record
     * PKT_$SEND_INTERNET and NET_IO_$SEND share.
     */
    send_info->port_net = ROUTE_$PORTP[port]->port_type;
    send_info->xmit_status = 0;

    /* 0x00E0DB24  cmp.l NODE_$ME,D4 / bne 0x00e0dbe0 */
    if (dest_node == NODE_$ME) {
        /* ---- local delivery ------------------------------------------- */

        /* 0x00E0DB2E  tst.w D2w (a WORD test, so 0x8000 counts as non-zero) */
        if (data_len != 0) {
            /*
             * 0x00E0DB32 - 0x00E0DB46.  Note the status this one reports goes
             * straight into the CALLER's status_ret, not into local_status.
             */
            PKT_$COPY_TO_PA((char *)data, data_len, data_pages, status_ret);
        } else {
            *status_ret = status_$ok;       /* 0x00E0DB4C  clr.l (A4) */
        }

        /* 0x00E0DB4E  bset.b #0x3,(0x3,A3) - bit 3 of xmit_status' LOW byte */
        send_info->xmit_status |= MSG_XMIT_LOCAL;

        /* 0x00E0DB54  tst.l (A4) / bne 0x00e0dbba */
        if (*status_ret == status_$ok) {
            /*
             * 0x00E0DB58 - 0x00E0DB88: the sock_$pkt_info_t SOCK_$PUT wants.
             * Only these fields are written; the rest of the 0x40-byte record
             * keeps whatever was on the stack.
             */
            local_pkt.hdr = hdr_va;                     /* 0x00E0DB58 */
            local_pkt.hdr_len = hdr_len;                /* 0x00E0DB5C, +0x2C */
            local_pkt.data_len = data_len;              /* 0x00E0DB60, +0x2A */
            local_pkt.n_hops = 0;                       /* 0x00E0DB64, +0x12 */

            /*
             * 0x00E0DB68  pea (-0x3c,A6) - the 6-byte clock is written
             * straight onto the record at +0x04, i.e. over the src_addr /
             * src_port pair SOCK_$GET would otherwise fill in from the
             * netbuf.  Spelled as a local plus two stores so the two halves
             * stay addressable off m68k.
             */
            {
                clock_t now;

                TIME_$ABS_CLOCK(&now);
                local_pkt.src_addr = now.high;
                local_pkt.src_port = now.low;
            }

            local_pkt.flags = 0;                        /* 0x00E0DB74, +0x10 */

            /* 0x00E0DB78 - 0x00E0DB88: four longwords, data_pages -> +0x30 */
            for (i = 0; i <= 3; i++) {
                local_pkt.data_pages[i] = data_pages[i];
            }

            /*
             * 0x00E0DB8C - 0x00E0DBA8.  The two words are
             * ROUTE_$PORT_ARRAY[0].port_type (0xE2E0CE) and .socket
             * (0xE2E0D0); the boolean pushed with "st" is true.
             *
             * SOCK_$PUT hands its second argument straight through to
             * SOCK_$PUT_INT_INT, which uses it AS the record
             * ("movea.l (0xc,A6),A2" at 0x00E16206).
             */
            {
                int8_t queued = SOCK_$PUT(dest_sock, &local_pkt, true,
                                          ROUTE_$PORT_ARRAY[0].port_type,
                                          ROUTE_$PORT_ARRAY[0].socket);

                /*
                 * 0x00E0DBAC  andi.b #0x7f,(0x2,A3)   clear bit 15
                 * 0x00E0DBB2  andi.b #-0x80,D0b       keep the boolean's sign
                 * 0x00E0DBB6  or.b D0b,(0x2,A3)
                 */
                send_info->xmit_status =
                    (uint16_t)((send_info->xmit_status & 0x7FFFu) |
                               (queued < 0 ? MSG_XMIT_QUEUED : 0u));
            }
        }

        /* 0x00E0DBBA  tst.w (0x2,A3) / bmi - the socket now owns the buffer */
        if ((int16_t)send_info->xmit_status < 0) {
            return;
        }

        NETBUF_$RTN_HDR(&hdr_va);                       /* 0x00E0DBC2 */
        PKT_$DUMP_DATA(data_pages, (int16_t)data_len);  /* 0x00E0DBD6 */
        return;                                         /* 0x00E0DBDC */
    }

    /* ---- remote delivery ----------------------------------------------- */

    used_dpage = 0;                 /* 0x00E0DBE0  clr.b D3b */

    /* 0x00E0DBE2  tst.w D2w */
    if (data_len != 0) {
        /*
         * 0x00E0DBE6 - 0x00E0DBEE: claim the bounce page by pre-incrementing
         * its counter; only a result of exactly zero means it was free.
         */
        MSG_$DPAGE->in_use++;

        if (MSG_$DPAGE->in_use == 0 && data_len <= MSG_DPAGE_MAX_DATA) {
            used_dpage = -1;                            /* 0x00E0DBF6  st D3b */
            data_va = MSG_$DPAGE->va;                   /* 0x00E0DBF8 */
            data_pages[0] = MSG_$DPAGE->pa;             /* 0x00E0DBFE */
            OS_$DATA_COPY((char *)data, (char *)ARCH_VA_TO_PTR(data_va),
                          (uint32_t)data_len);          /* 0x00E0DC04-0x00E0DC18 */
        } else {
            MSG_$DPAGE->in_use--;                       /* 0x00E0DC1E */

            /* 0x00E0DC22 - 0x00E0DC38 */
            PKT_$COPY_TO_PA((char *)data, data_len, data_pages, &local_status);
            if (local_status != status_$ok) {
                goto release;                           /* 0x00E0DC40 */
            }
            data_va = 0;                                /* 0x00E0DC42 */
        }
    } else {
        data_va = 0;                                    /* 0x00E0DC42 */
    }

    send_flags = 0;                                     /* 0x00E0DC46 */

    ML_$LOCK(ML_LOCK_NET_IO);                          /* 0x00E0DC4A */

    /* 0x00E0DC58 - 0x00E0DC80 */
    NET_IO_$SEND(port, &hdr_va, hdr_pa, hdr_len, data_va, data_pages,
                 (int16_t)data_len, send_flags, &net_info, &local_status);

    ML_$UNLOCK(ML_LOCK_NET_IO);                        /* 0x00E0DC84 */

    *send_info = net_info;                              /* 0x00E0DC92, one long */

release:
    NETBUF_$RTN_HDR(&hdr_va);                           /* 0x00E0DC96 */

    /* 0x00E0DCA2  tst.b D3b / bpl - see the note on used_dpage above */
    if (used_dpage < 0) {
        MSG_$DPAGE->in_use--;                           /* 0x00E0DCA6 */
    } else {
        PKT_$DUMP_DATA(data_pages, (int16_t)data_len);  /* 0x00E0DCB4 */
    }

    *status_ret = local_status;                         /* 0x00E0DCBA */
}

/*
 * MSG_$SENDI - by-reference wrapper around MSG_$$SEND
 *
 * Original address: 0x00E59AA6
 */
void MSG_$SENDI(uint32_t *routing_key,
                uint32_t *dest_node,
                uint16_t *dest_sock,
                int32_t *src_node_or,
                uint32_t *src_node,
                uint16_t *src_sock,
                void *pkt_info,
                uint16_t *request_id,
                void *template,
                uint16_t *template_len,
                void *data,
                uint16_t *data_len,
                uint16_t *xmit_status,
                status_$t *status_ret)
{
    net_io_$send_info_t send_info;      /* A6-0x4 */

    /* 0x00E59AAE - 0x00E59AFC, pushed right to left; the port is fixed at -1 */
    MSG_$$SEND(-1, *routing_key, *dest_node, *dest_sock, *src_node_or,
               *src_node, *src_sock, (const pkt_$info_t *)pkt_info,
               *request_id, template, *template_len, data, *data_len,
               &send_info, status_ret);

    /* 0x00E59B02  move.w (-0x2,A6),(A0) - the SECOND word of the record */
    *xmit_status = send_info.xmit_status;
}

/*
 * MSG_$SEND - send to a socket using the MSG module's packet-info template
 *
 * Original address: 0x00E599FC
 */
void MSG_$SEND(uint32_t *dest_node,
               uint16_t *dest_sock,
               uint16_t *src_sock,
               uint16_t *info_flags,
               uint16_t *request_id,
               void *template,
               uint16_t *template_len,
               void *data,
               uint16_t *data_len,
               uint16_t *xmit_status,
               status_$t *status_ret)
{
    pkt_$info_t info;                   /* A6-0x20 */
    net_io_$send_info_t send_info;      /* A6-0x24 */
    uint32_t node;                      /* A6-0x28 */
    uint16_t dsock;                     /* A6-0x34 */
    uint16_t ssock;                     /* A6-0x32 */
    uint16_t flags;                     /* D0 */
    uint16_t id;                        /* A6-0x2E */
    uint16_t tpl_len;                   /* A6-0x2C */
    uint16_t dat_len;                   /* A6-0x2A */
    int i;

    /* 0x00E59A0A - 0x00E59A3C: every scalar argument is dereferenced first */
    node = *dest_node;
    dsock = *dest_sock;
    ssock = *src_sock;
    flags = *info_flags;
    id = *request_id;
    tpl_len = *template_len;
    dat_len = *data_len;

    /*
     * 0x00E59A40 - 0x00E59A4E: 30 bytes out of MSG_$DATA's template.
     * pkt_$info_t's trailing pad word is left as the stack found it.
     */
    for (i = 0; i < 0x1E; i++) {
        ((uint8_t *)&info)[i] = MSG_$DATA->send_template[i];
    }

    /* 0x00E59A50  move.w D0w,(-0x20,A6) */
    info.flags = flags;

    /*
     * 0x00E59A54 - 0x00E59A8E.  Port -1, routing key 0, no source override,
     * and NODE_$ME as the source node.
     */
    MSG_$$SEND(-1, 0, node, dsock, 0, NODE_$ME, ssock, &info, id,
               template, tpl_len, data, dat_len, &send_info, status_ret);

    /* 0x00E59A94  move.w (-0x22,A6),(A1) */
    *xmit_status = send_info.xmit_status;
}
