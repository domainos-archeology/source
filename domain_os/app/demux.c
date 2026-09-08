/*
 * APP_$DEMUX - the channel demux vector APP_$STD_OPEN installs
 *
 * Called by XNS_IDP_$OS_DEMUX through xns_$channel_t.demux (0x00E18658) with
 * the packet descriptor it built at A6-0x88.  APP_$DEMUX turns that
 * descriptor into a sock_$pkt_info_t and queues it on the socket named in
 * the application header, falling back to the overflow socket when the file
 * socket is full and returning the buffers when nothing took the packet.
 *
 * Original address: 0x00E00A90
 */

#include "app/app_internal.h"

/*
 * @param pkt            the descriptor XNS_IDP_$OS_DEMUX built (A6+0x08)
 * @param port_type      the ROUTE port's type word   (A6+0x0C, read as *D2)
 * @param port_socket    the ROUTE port's socket word (A6+0x10, read as *D3)
 * @param mac_broadcast  Domain boolean: the frame arrived as a MAC broadcast
 * @param status_ret     Output: status code.  Cleared at 0x00E00AAC and never
 *                       touched again - A2 is immediately reused for the
 *                       application header.
 */
void APP_$DEMUX(xns_$pkt_desc_t *pkt, uint16_t *port_type,
                uint16_t *port_socket, boolean *mac_broadcast,
                status_$t *status_ret)
{
    /*
     * The record SOCK_$PUT is given, built at A6-0x40 and passed with
     * "pea (-0x40,A6)" (0x00E00B20 and 0x00E00B4A).  It is one
     * sock_$pkt_info_t, not a handful of adjacent locals: A6-0x40 is its
     * +0x00, A6-0x30 its +0x10, A6-0x16 its +0x2A, A6-0x14 its +0x2C and
     * A6-0x10 its +0x30.
     */
    sock_$pkt_info_t rec;
    const app_pkt_hdr_t *app_hdr;
    int8_t put_result;
    int i;

    *status_ret = status_$ok;                           /* 0x00E00AAC */

    rec.hdr = pkt->header;                              /* 0x00E00AAE */

    /*
     * 0x00E00AB4 "moveq #0x1e,D0 / add.l (-0x40,A6),D0": the application
     * header follows the 30-byte IDP header.
     */
    app_hdr = (const app_pkt_hdr_t *)ARCH_VA_TO_PTR(rec.hdr + XNS_IDP_HEADER_SIZE);

    /*
     * 0x00E00ABC-0x00E00AD6.  All four tests must hold for the packet to be
     * dropped without ever being queued: the longword at +0x08 is 2, the word
     * at +0x0C is 4, the byte at +0x14 is negative, and the caller's boolean
     * is TRUE ("tst.b (A1) / bmi.w 0x00E00B66").
     */
    if (app_hdr->src_node == 2 &&                       /* 0x00E00ABC, a LONG */
        app_hdr->src_sock == 4 &&                       /* 0x00E00AC6, a word */
        (int8_t)app_hdr->net_type < 0 &&                /* 0x00E00ACE `bpl' */
        *mac_broadcast < 0) {                           /* 0x00E00AD4 `bmi' */
        goto return_buffers;
    }

    /*
     * 0x00E00ADA-0x00E00AE8.  The flags word is built the way every port
     * demux builds it: the constant 2 (SOCK_PKT_FLAG_XNS), then a byte bset
     * on the word's LOW half, which is word bit 2.
     */
    rec.flags = SOCK_PKT_FLAG_XNS;
    if (*mac_broadcast < 0) {
        rec.flags |= SOCK_PKT_FLAG_DEMUX_BOOL;
    }

    rec.src_addr = pkt->mac_src_hi;                     /* 0x00E00AEA */
    rec.src_port = pkt->mac_src_lo;                     /* 0x00E00AF0 */

    /*
     * 0x00E00AF6 "move.w (0x1a,A0),(-0x14,A6)" reads the LOW word of the
     * descriptor's longword data_len at +0x18, and lands in the record's
     * hdr_len at +0x2C.
     */
    rec.hdr_len = (uint16_t)pkt->data_len;

    /* 0x00E00AFC "move.w (0x36,A0),(-0x16,A6)": the descriptor's +0x36 into
     * the record's data_len at +0x2A. */
    rec.data_len = pkt->port_info;

    /* 0x00E00B02-0x00E00B10: four longwords from the descriptor's +0x38. */
    for (i = 0; i < 16; i++) {
        ((uint8_t *)rec.data_pages)[i] =
            ((const uint8_t *)pkt)[offsetof(xns_$pkt_desc_t, mac_info) + i];
    }

    rec.n_hops = 0;                                     /* 0x00E00B12 */

    /*
     * 0x00E00B16-0x00E00B2E.  Pushed right to left: the socket named in the
     * application header, the record, a zero flag word, then the two words
     * the caller pointed at.
     */
    put_result = SOCK_$PUT(app_hdr->src_sock, &rec, 0,
                           *port_type, *port_socket);
    if (put_result < 0) {                               /* 0x00E00B32 `bmi' */
        return;                                         /* -> 0x00E00B88 */
    }

    /*
     * TODO(source-klc1): app_pkt_hdr_t calls +0x08/+0x0C the SOURCE node and
     * socket, but APP_$DEMUX delivers the packet to the socket at +0x0C and
     * compares it against APP_SOCK_TYPE_FILE, so on an incoming packet the
     * pair reads as the DESTINATION.  The field names are left as the rest of
     * app/ has them until a writer of the header settles the direction.
     */
    if (app_hdr->src_sock != APP_SOCK_TYPE_FILE) {      /* 0x00E00B36 */
        goto return_buffers;
    }

    RING_$FILE_OVERFLOW += 1;                           /* 0x00E00B3E */

    /* 0x00E00B44-0x00E00B58: the same record, on the overflow socket. */
    put_result = SOCK_$PUT(APP_SOCK_TYPE_OVERFLOW, &rec, 0,
                           *port_type, *port_socket);
    if (put_result < 0) {                               /* 0x00E00B5C */
        return;
    }

    RING_$OVERFLOW_OVERFLOW += 1;                       /* 0x00E00B60 */

return_buffers:
    NETBUF_$RTN_HDR(&rec.hdr);                          /* 0x00E00B6A */

    if (rec.data_pages[0] != 0) {                       /* 0x00E00B72 */
        /* 0x00E00B78 "subq.l #0x2,SP" is the Pascal result slot. */
        PKT_$DUMP_DATA(rec.data_pages, (int16_t)rec.data_len);   /* 0x00E00B82 */
    }
}
