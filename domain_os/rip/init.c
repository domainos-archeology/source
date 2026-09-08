/*
 * rip/init.c - RIP_$INIT (0x00E2FBD0, 538 bytes)
 *
 * Initialises the RIP subsystem's three exclusion locks and, on a diskless
 * node, asks the mother node for the network number this node lives on.
 *
 * Re-emitted against the listing for bead source-bkr7.  Corrections: the
 * APP_$RECEIVE output is the standard `app_$receive_rec_t` (route port at
 * +0x18, the payload VA that NETBUF_$RTN_HDR is given at +0x04, the page
 * vector at +0x08), and PKT_$SEND_INTERNET's template and data arguments are
 * two real constant cells, not NULL.
 *
 * Frame: `link.w A6,-0x90` + a seven-register movem (0x1C bytes), so the
 * epilogue is `movem.l (-0xac,A6)` (0x00E2FDE0).
 */

#include "rip/rip_internal.h"
#include "arch/arch.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"
#include "app/app.h"

/*
 * The request template PKT_$SEND_INTERNET is given, with a template length of
 * 2 (`pea (A5)` at 0x00E2FCA6, A5 = 0x00E3502C from `lea (0xe3502c).l,A5` at
 * 0x00E2FBD8).
 *
 * The SAU2 10.2 map calls it "D E3502C RIP_WIRED size = 4", a four-byte cell
 * inside the OS_INIT_DATA segment with no interior symbol; `gsk read
 * 0x00E3502C 4` gives 00 00 00 00, so the two transmitted bytes are zero.
 */
uint32_t RIP_$INIT_REQUEST;

/*
 * The data pointer the same call is given, with a data length of 0
 * (`pea (0x14c,PC)` at 0x00E2FC9E, extension word 0x00E2FCA0 + 0x14C =
 * 0x00E2FDEC).  It is a literal cell in the code region, just after the `rts`
 * at 0x00E2FDE8; `gsk read 0x00E2FDEC 4` gives 00 00 00 00.  Nothing reads
 * through it, because PKT_$SEND_INTERNET only touches the data pointer when
 * the length is positive.
 */
static const uint32_t rip_$init_nil_data = 0;

/*
 * The 30-byte packet-info template lives at RIP_$DATA+0xC68 and is copied to
 * the frame before use (7 longwords plus a word, 0x00E2FC28-0x00E2FC36).
 */
#define RIP_INIT_PKT_INFO_LEN   30

/* The mother node's RIP socket (`move.w #0x1,-(SP)` at 0x00E2FCB8). */
#define RIP_INIT_MOTHER_SOCKET  1

void RIP_$INIT(void)
{
    uint8_t   pkt_info[RIP_INIT_PKT_INFO_LEN];  /* A6-0x50 */
    uint32_t  hdr_va;                           /* A6-0x54 */
    app_$receive_rec_t rcv;                     /* A6-0x30 */
    int32_t   deadline;                         /* A6-0x70 */
    int32_t   sock_wait_val;                    /* A6-0x6C */
    status_$t status;                           /* A6-0x74 */
    uint16_t  resp_len;                         /* A6-0x76 */
    uint16_t  retry_hint;                       /* A6-0x88 */
    uint16_t  timeout_out;                      /* A6-0x86 */
    uint16_t  sock_num;                         /* A6-0x8A */
    int16_t   pkt_id;                           /* D4 */
    int16_t   reply_id;                         /* D3 */
    uint32_t  route_port;                       /* D5 */
    ec_$eventcount_t *sock_ec;                  /* A3 */
    ec_$eventcount_t *terminator;               /* A3, reused at 0x00E2FCF6 */
    int       i;

    /* 0x00E2FBDE-0x00E2FC12.  The three locks are reached through the literal
     * base 0x00E26258 (RIP_$DATA), not through A5. */
    ML_$EXCLUSION_INIT(&RIP_$DATA.exclusion);           /* +0x40 */
    ML_$EXCLUSION_INIT(&RIP_$DATA.route_service_mutex); /* +0x28 */
    ML_$EXCLUSION_INIT(&RIP_$DATA.xns_error_mutex);     /* +0x10 */

    /* 0x00E2FC14: only a diskless node goes looking for its network. */
    if (NETWORK_$DISKLESS >= 0) {
        return;
    }

    /* 0x00E2FC1E-0x00E2FC38 */
    for (i = 0; i < RIP_INIT_PKT_INFO_LEN; i++) {
        pkt_info[i] = RIP_$DATA.bcast_control[i];
    }
    pkt_info[1] &= (uint8_t)~0x80u;                 /* `bclr.b #0x7` */

    /* 0x00E2FC3E-0x00E2FC5E.  SOCK_$ALLOCATE's boolean result is 0xFF on
     * success, so a non-negative result is the failure. */
    if (SOCK_$ALLOCATE(&sock_num, 0x00010001, 0x00010400) >= 0) {
        return;
    }

    /* 0x00E2FC62-0x00E2FC82.  The table at 0xE28DB4 is indexed with a -4
     * displacement, so socket n's entry is SOCK_$EVENT_COUNTERS[n - 1]. */
    sock_ec = SOCK_$EVENT_COUNTERS[sock_num - 1];
    sock_wait_val = EC_$READ(sock_ec) + 1;

    pkt_id = PKT_$NEXT_ID();                        /* 0x00E2FC86 */

    /* 0x00E2FC8E-0x00E2FCCA: fifteen arguments plus a 2-byte result slot. */
    PKT_$SEND_INTERNET(0,                           /* routing_key */
                       NETWORK_$MOTHER_NODE,        /* dest_node */
                       RIP_INIT_MOTHER_SOCKET,      /* dest_sock */
                       0,                           /* src_node_or */
                       NODE_$ME,                    /* src_node */
                       sock_num,                    /* src_sock */
                       pkt_info,                    /* pkt_info */
                       (uint16_t)pkt_id,            /* request_id */
                       &RIP_$INIT_REQUEST,          /* template  0x00E3502C */
                       2,                           /* template_len */
                       (void *)&rip_$init_nil_data, /* data      0x00E2FDEC */
                       0,                           /* data_len */
                       &retry_hint,                 /* A6-0x88 */
                       &timeout_out,                /* A6-0x86 */
                       &status);

    /* 0x00E2FCCE-0x00E2FCD6.  D3 is loaded from the timeout output BEFORE the
     * status is tested. */
    if (status != status_$ok) {
        goto close_socket;
    }

    /* 0x00E2FCDA-0x00E2FCF2.  Only the low 16 bits of the timeout are used. */
    deadline = EC_$READ((ec_$eventcount_t *)&TIME_$CLOCKH) +
               (int32_t)(uint32_t)(uint16_t)timeout_out + 1;

    /* 0x00E2FCF6: A3 becomes the NIL terminator of the eventcount list. */
    terminator = NULL;

    for (;;) {
        int16_t which;

        /* 0x00E2FDA8-0x00E2FDD2.  Six longwords, no result slot; the value
         * slot behind the NIL terminator still carries the 1 that
         * `pea (0x1).w` pushes.  `tst.w D0w / seq D3b / bmi` receives only
         * when the result is zero, i.e. the socket fired. */
        which = EC_$WAIT((ec_$wait_ecs_t){{ SOCK_$EVENT_COUNTERS[sock_num - 1],
                                            (ec_$eventcount_t *)&TIME_$CLOCKH,
                                            terminator }},
                         (ec_$wait_vals_t){{ sock_wait_val, deadline, 1 }});
        if (which != 0) {
            goto close_socket;
        }

        /* 0x00E2FD00-0x00E2FD12 */
        APP_$RECEIVE(sock_num, &rcv, &status);

        /* 0x00E2FD16-0x00E2FD30 */
        route_port = rcv.hdr_f06;               /* rcv+0x18 */
        {
            const rip_$init_reply_hdr_t *reply =
                (const rip_$init_reply_hdr_t *)ARCH_VA_TO_PTR(rcv.reply);
            resp_len = reply->data_len;         /* reply+0x04 */
            reply_id = (int16_t)reply->reply_id; /* reply+0x06 */
        }
        /* `andi.w #-0x400,D0w` masks only the low word, which is the same as
         * masking the longword with 0xFFFFFC00. */
        hdr_va = rcv.data & 0xFFFFFC00u;        /* rcv+0x04 */

        NETBUF_$RTN_HDR(&hdr_va);               /* 0x00E2FD38 */

        if (status != status_$ok) {             /* 0x00E2FD40 */
            goto close_socket;
        }

        /* 0x00E2FD48-0x00E2FD5E */
        if (rcv.data_pages[0] != 0) {
            PKT_$DUMP_DATA(rcv.data_pages, (int16_t)resp_len);
        }

        /* 0x00E2FD60: a reply for some other request is discarded. */
        if (reply_id == pkt_id) {
            break;
        }
    }

    /* 0x00E2FD64-0x00E2FD70 */
    ROUTE_$PORT = route_port;
    RIP_$DATA.route_port = route_port;

    /* 0x00E2FD72-0x00E2FDA2.  The route-port cell doubles as the source
     * address: RIP_$DATA's first ten bytes are read as a rip_$xns_addr_t.
     * The two calls differ only in the boolean, which picks the non-standard
     * route table on the second pass (`st` at 0x00E2FD90). */
    RIP_$UPDATE_INT(route_port, (rip_$xns_addr_t *)&RIP_$DATA, 0, 0, false, &status);
    RIP_$UPDATE_INT(route_port, (rip_$xns_addr_t *)&RIP_$DATA, 0, 0, true, &status);

close_socket:                                   /* 0x00E2FDD6 */
    SOCK_$CLOSE(sock_num);
}
