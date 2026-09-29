/*
 * XNS Error Protocol Send
 *
 * XNS_ERROR_$SEND (0x00E17A2E) and the four helpers that share its module
 * data area: xns_$is_local_addr lives in xns/idp_helpers.c, the other three
 * are static here because nothing outside the module calls them.
 *
 *   xns_$maybe_open_error_socket   0x00E178AA
 *   xns_$maybe_close_error_socket  0x00E17910
 *   xns_$pkt_bufs_in_netbuf_pool   0x00E17876
 *   xns_$setup_error_header        0x00E17960 (nested in XNS_ERROR_$SEND)
 *
 * The XNS Error Protocol reports an undeliverable packet back to whoever
 * sent it: the reply carries the first 0x2A bytes of the offending packet
 * behind an error code and parameter.
 *
 * Original address: 0x00E17A2E
 *
 * Module data through XNS_IDP_$DATA / XNS_ERROR_$DATA: Claude Opus 5.5
 * (source-iq58).
 */

#include "xns/xns_internal.h"

/*
 * The XNS_ERROR module data segment, 0x00E2B29C (SAU2 link map, size 0x78),
 * is the MODULE_DATA block XNS_ERROR_$DATA (xns/xns_internal.h, defined
 * with its image contents in xns/xns_data.c).  Every routine in this file
 * starts with "lea (0xe2b29c).l,A5", so each A5 displacement below is a
 * field of that block.
 */

/*
 * XNS_ERROR_$CLIENT_MUTEX, 0x00E26268 (SAU2 link map), lies in the RIP_WIRED
 * segment: it is RIP_$WIRED_DATA.xns_error_mutex (rip/rip.h), zero in the
 * image and set up by RIP_$INIT's ML_$EXCLUSION_INIT (0x00E2FC08).
 */

/*
 * xns_$maybe_open_error_socket (0x00E178AA)
 *
 * Reference-counted open of the error protocol's IDP channel, under
 * XNS_ERROR_$CLIENT_MUTEX.  The channel is opened only on the transition
 * from zero clients, but the count is bumped on EVERY successful call.
 */
static void xns_$maybe_open_error_socket(status_$t *status_ret)
{
    xns_$os_open_opt_t open_opt;        /* A6-0x28 */

    *status_ret = status_$ok;                           /* 0x00E178BC clr.l (A2) */

    ML_$EXCLUSION_START(&RIP_$WIRED_DATA.xns_error_mutex);      /* 0x00E178BE */

    if (XNS_ERROR_$DATA.client_ref_count == 0) {        /* 0x00E178CC tst.w (0x74,A5) */
        /*
         * 0x00E178D2 "move.l #0x30008,(-0x28,A6)" writes both halves of the
         * record's first longword at once: socket 3 (the error protocol's
         * well-known socket) and the flag word 0x0008, whose low byte is
         * XNS_OPEN_FLAG_NO_ALLOC.  0x00E178DA "clr.l (-0x24,A6)" then clears
         * the demux vector; nothing else in the record is initialised because
         * neither the bind nor the connect flag is set.
         */
        open_opt.socket        = XNS_SOCKET_ERROR;
        open_opt.flags_channel = XNS_OPEN_FLAG_NO_ALLOC;
        open_opt.demux         = 0;

        XNS_IDP_$OS_OPEN(&open_opt, status_ret);        /* 0x00E178E4 */

        if (*status_ret != status_$ok) {                /* 0x00E178EC tst.l (A2) */
            /* 0x00E178EE "bne.b 0x00E178FA" skips the increment. */
            ML_$EXCLUSION_STOP(&RIP_$WIRED_DATA.xns_error_mutex);
            return;
        }

        /* 0x00E178F0 "move.w (-0x26,A6),(0x76,A5)": the record's +0x02 now
         * holds the channel index XNS_IDP_$OS_OPEN wrote there. */
        XNS_ERROR_$DATA.std_idp_channel = (int16_t)open_opt.flags_channel;
    }

    XNS_ERROR_$DATA.client_ref_count += 1;              /* 0x00E178F6 */

    ML_$EXCLUSION_STOP(&RIP_$WIRED_DATA.xns_error_mutex);       /* 0x00E178FA */
}

/*
 * xns_$maybe_close_error_socket (0x00E17910)
 *
 * The mirror of the above: drop one client and close the channel when the
 * last one goes away.
 */
static void xns_$maybe_close_error_socket(void)
{
    status_$t close_status;             /* A6-0x04 */

    ML_$EXCLUSION_START(&RIP_$WIRED_DATA.xns_error_mutex);      /* 0x00E1791C */

    if (XNS_ERROR_$DATA.client_ref_count == 0) {        /* 0x00E1792A tst.w (0x74,A5) */
        /*
         * QUIRK, reproduced as found: 0x00E1792E "beq.b 0x00E17958" branches
         * PAST the ML_$EXCLUSION_STOP straight to the epilogue
         * ("movea.l (-0x8,A6),A5 / unlk / rts"), so an unbalanced call leaves
         * XNS_ERROR_$CLIENT_MUTEX held for good.
         */
        return;
    }

    XNS_ERROR_$DATA.client_ref_count -= 1;              /* 0x00E17930 */

    if (XNS_ERROR_$DATA.client_ref_count == 0) {        /* 0x00E17934 bne.b 0x00E1794C */
        /* 0x00E17936 "pea (-0x4,A6)" then 0x00E1793A "pea (0x76,A5)" - the
         * channel cell itself is the argument. */
        XNS_IDP_$OS_CLOSE(&XNS_ERROR_$DATA.std_idp_channel, &close_status);
        XNS_ERROR_$DATA.std_idp_channel = -1;           /* 0x00E17946 */
    }

    ML_$EXCLUSION_STOP(&RIP_$WIRED_DATA.xns_error_mutex);       /* 0x00E1794C */
}

/*
 * XNS_ERROR_$SEND's frame, as far as its nested procedure reaches into it.
 *
 * xns_$setup_error_header (0x00E17960) takes NO arguments -- 0x00E17B1E is a
 * bare `bsr.w` with nothing pushed -- and reads the parent through the Pascal
 * static link, `movea.l (A6),A2` at 0x00E17968.  In a freshly `link`ed frame
 * (A6) is the saved old A6, so A2 is XNS_ERROR_$SEND's own frame pointer and
 * every displacement it uses is a slot of that frame.  The two positive ones
 * are XNS_ERROR_$SEND's own arguments.
 */
typedef struct xns_$error_send_frame_t {
    xns_$pkt_desc_t *packet_info;   /* A6+0x08: argument 1 */
    status_$t       *status_ret;    /* A6+0x18: argument 5 */
    uint32_t         netbuf_va;     /* A6-0x28: NETBUF_$GET_HDR's VA */
    xns_$pkt_desc_t *packet_info_2; /* A6-0x34: 0x00E17A6A copies argument 1 */
    int16_t          remaining;     /* A6-0x36: bytes of the original packet
                                     *          still to be appended */
    boolean          in_netbuf_pool;/* A6-0x3C: xns_$pkt_bufs_in_netbuf_pool */
} xns_$error_send_frame_t;

/*
 * The buffer-descriptor chain a received packet carries starts with the
 * triple embedded in the packet record at +0x18 - {data_len, header, iov} is
 * exactly a mac_os_$buf_desc_t {length, address, next}, and both
 * 0x00E17880 and 0x00E17978 form its address with `lea (0x18,A0),Ax`.
 *
 * Since bead source-ronb the three fields really do lay out as that
 * descriptor on the host as well (xns_$pkt_desc_t.header is a target VA, not
 * a C pointer, and the record is packed), which the asserts below check.
 * The head is still returned BY VALUE rather than as a pointer into the
 * record: taking the address of a member of a packed struct is what
 * -Waddress-of-packed-member exists to stop, and nothing ever writes through
 * this descriptor.
 */
_Static_assert(offsetof(mac_os_$buf_desc_t, length) == 0x00,
               "buf_desc.length at +0x00");
_Static_assert(offsetof(xns_$pkt_desc_t, header) - offsetof(xns_$pkt_desc_t, data_len) ==
               offsetof(mac_os_$buf_desc_t, address),
               "buf_desc.address lines up with pkt_desc.header");
_Static_assert(offsetof(xns_$pkt_desc_t, iov) - offsetof(xns_$pkt_desc_t, data_len) ==
               offsetof(mac_os_$buf_desc_t, next),
               "buf_desc.next lines up with pkt_desc.iov");

static mac_os_$buf_desc_t xns_$pkt_head_desc(const xns_$pkt_desc_t *packet_info)
{
    mac_os_$buf_desc_t head;

    head.length  = (int32_t)packet_info->data_len;
    head.address = packet_info->header;     /* already a target VA */
    head.next    = packet_info->iov;
    return head;
}

/*
 * xns_$pkt_bufs_in_netbuf_pool (0x00E17876)
 *
 * Walks the received packet's buffer-descriptor chain and returns TRUE only
 * if EVERY descriptor's address lies in the network-buffer window
 * [buf_va_low, buf_va_high).  Both bounds are module data cells that nothing
 * else in the image writes, so the window is the constant
 * [0x00D64C00, 0x00D94C00) they are loaded with.
 *
 * The chain head is the descriptor embedded in the packet record at +0x18
 * (0x00E17880 `lea (0x18,A0),A0`), i.e. {data_len, header, iov}; the links
 * are each node's +0x08.
 *
 * The result decides how xns_$setup_error_header fetches the rest of the
 * original packet: TRUE means the payload lives in network buffers and must
 * be paged in with NETBUF_$GETVA, FALSE means the descriptor chain can simply
 * be walked.
 */
static boolean xns_$pkt_bufs_in_netbuf_pool(const xns_$pkt_desc_t *packet_info)
{
    const mac_os_$buf_desc_t *node;
    mac_os_$buf_desc_t head;
    boolean all_in_pool;

    all_in_pool = true;                 /* 0x00E1787E `st D0b` */

    /* 0x00E17880 `lea (0x18,A0),A0`. */
    head = xns_$pkt_head_desc(packet_info);
    node = &head;

    /* 0x00E178A0 `cmpa.w #0x0,A0` / `bne` - the test is at the BOTTOM, so the
     * head is examined first (0x00E17884 `bra.b`). */
    while (node != NULL) {
        int32_t va = (int32_t)node->address;    /* 0x00E17888 */

        if (va < XNS_ERROR_$DATA.buf_va_low) {  /* 0x00E1788C / `blt` */
            all_in_pool = false;                /* 0x00E17898 `clr.b D0b` */
            break;
        }
        if (va >= XNS_ERROR_$DATA.buf_va_high) { /* 0x00E17892 / `blt` to next */
            all_in_pool = false;
            break;
        }
        node = (const mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(node->next);
    }

    return all_in_pool;
}

/*
 * xns_$setup_error_header (0x00E17960)
 *
 * Appends up to 0x2A bytes of the offending packet to the error packet being
 * built, starting at offset 0x22 of the new header buffer (the IDP header the
 * caller fills in afterwards occupies 0x00..0x21).
 *
 * It is a nested procedure of XNS_ERROR_$SEND; see xns_$error_send_frame_t.
 * The copy source is either the packet's descriptor chain or, when the
 * payload sits in the network buffer pool, successive pages fetched with
 * NETBUF_$GETVA.  The last fetched page is handed back on the way out.
 */
static void xns_$setup_error_header(xns_$error_send_frame_t *parent)
{
    uint32_t fetched_va;                /* D2: last NETBUF_$GETVA page, 0 = none */
    uint32_t src_va;                    /* D4: current copy source */
    int32_t  chunk;                     /* D5: bytes to copy this time round */
    int16_t  dst_off;                   /* D3: offset into the new header buffer */
    const mac_os_$buf_desc_t *node;     /* A3 */
    mac_os_$buf_desc_t head;
    uint32_t getva_va;                  /* A6-0x10 */

    fetched_va = 0;                     /* 0x00E1796A `clr.l D2` */
    parent->remaining = 0x2A;           /* 0x00E1796C `move.w #0x2a,(-0x36,A2)` */
    dst_off = 0x22;                     /* 0x00E17972 `moveq #0x22,D3` */

    /* 0x00E17974 `movea.l (0x8,A2),A0` / 0x00E17978 `lea (0x18,A0),A3`. */
    head = xns_$pkt_head_desc(parent->packet_info);
    node = &head;

    /*
     * 0x00E1797C `bra.b 0x00E179F6` enters at the "load this node" step, and
     * 0x00E17A06 is the loop test that every arm falls back to.
     */
    src_va = node->address;                             /* 0x00E179F6 */
    chunk  = parent->remaining;                         /* 0x00E179FA */
    if (chunk > node->length) {                         /* 0x00E17A00 / `ble` */
        chunk = node->length;                           /* 0x00E17A04 */
    }

    /* 0x00E17A06 `tst.w (-0x36,A2)` / 0x00E17A0C `tst.l D4`. */
    while (parent->remaining != 0 && src_va != 0) {
        /* 0x00E17980-0x00E1799C: dst = netbuf_va + dst_off. */
        uint32_t dst_va = parent->netbuf_va + (uint32_t)(int32_t)dst_off;

        OS_$DATA_COPY((char *)ARCH_VA_TO_PTR(src_va),
                      (char *)ARCH_VA_TO_PTR(dst_va),
                      chunk);

        dst_off = (int16_t)(dst_off + (int16_t)chunk);      /* 0x00E179A0 */
        parent->remaining =
            (int16_t)(parent->remaining - (int16_t)chunk);  /* 0x00E179A2 */

        if (parent->remaining == 0) {                       /* 0x00E179A6 */
            break;
        }

        if (parent->in_netbuf_pool < 0) {   /* 0x00E179AC `tst.b` / `bpl` */
            /*
             * 0x00E179B2-0x00E179E4: page in the next stretch of the payload.
             * The length is clamped against the packet record's +0x34, and
             * the handle is its +0x38.
             */
            const xns_$pkt_desc_t *pkt = parent->packet_info_2;

            chunk = parent->remaining;
            if (chunk > (int32_t)pkt->netbuf_len) {
                chunk = (int32_t)pkt->netbuf_len;
            }

            NETBUF_$GETVA(pkt->netbuf_handle, &getva_va, parent->status_ret);
            fetched_va = getva_va;
            src_va = fetched_va;
            continue;                       /* 0x00E179E6 `bra.b 0x00E17A06` */
        }

        node = (const mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(node->next);
        if (node == NULL) {                 /* 0x00E179EC `cmpa.w #0x0,A3` */
            src_va = 0;                     /* 0x00E179F2 `clr.l D4` */
            continue;
        }

        src_va = node->address;                         /* 0x00E179F6 */
        chunk  = parent->remaining;
        if (chunk > node->length) {
            chunk = node->length;
        }
    }

    /* 0x00E17A12-0x00E17A1E. */
    if (fetched_va != 0) {
        getva_va = fetched_va;
        NETBUF_$RTNVA(&getva_va);
    }
}

/*
 * XNS_ERROR_$SEND - send an XNS Error Protocol packet (0x00E17A2E)
 *
 * @param packet_info   the offending packet, as XNS_IDP_$OS_DEMUX built it
 * @param error_code    the XNS_ERROR_* code, read as a word (0x00E17B9E)
 * @param error_param   the error parameter, read as a word (0x00E17B96)
 * @param result_ret    output: the word XNS_IDP_$OS_SEND reports; cleared to
 *                      zero on entry (0x00E17A44 `clr.w (A0)`)
 * @param status_ret    output: status code
 *
 * NOTE, reproduced as found: every error exit branches to 0x00E17BEE, the
 * bare `movem/unlk/rts` epilogue.  Only the success path reaches the
 * FIM_$RLS_CLEANUP at 0x00E17BC2, so a rejected packet leaves the cleanup
 * handler this routine established still registered.
 */
void XNS_ERROR_$SEND(xns_$pkt_desc_t *packet_info, uint16_t *error_code,
                     uint16_t *error_param, uint16_t *result_ret,
                     status_$t *status_ret)
{
    uint32_t  netbuf_handle;            /* A6-0x2C */
    status_$t cleanup_status;           /* A6-0x20 */
    status_$t open_status;              /* A6-0x1C */
    uint8_t   cleanup_buf[24];          /* A6-0x18 */
    boolean   socket_opened;            /* A6-0x3A */
    int8_t    src_is_bcast;             /* A6-0x4A */
    xns_$error_send_frame_t frame;      /* the slots the nested procedure reads */
    const uint8_t *orig;                /* A2, first use: the offending header */
    xns_$error_pkt_t *pkt;              /* A2, second use: the new packet */
    int32_t   packet_len;               /* D1 at 0x00E17B26 */
    int i;

    netbuf_handle = 0;                  /* 0x00E17A3C `clr.l (-0x2c,A6)` */
    *result_ret = 0;                    /* 0x00E17A44 `clr.w (A0)` */
    *status_ret = status_$ok;           /* 0x00E17A4A `clr.l (A1)` */
    socket_opened = false;              /* 0x00E17A4C `clr.b (-0x3a,A6)` */

    frame.packet_info = packet_info;    /* A6+0x08, the argument itself */
    frame.status_ret  = status_ret;     /* A6+0x18, likewise */

    cleanup_status = FIM_$CLEANUP(cleanup_buf);         /* 0x00E17A54 */
    if (cleanup_status != status_$cleanup_handler_set) {
        /* 0x00E17BCA: the cleanup handler fired. */
        if (netbuf_handle != 0) {                       /* 0x00E17BCA tst.l */
            NETBUF_$RTN_HDR(&frame.netbuf_va);          /* 0x00E17BD0 */
        }
        if (socket_opened < 0) {                        /* 0x00E17BDC tst.b / bpl */
            xns_$maybe_close_error_socket();            /* 0x00E17BE2 */
        }
        *status_ret = cleanup_status;                   /* 0x00E17BEA */
        return;
    }

    frame.packet_info_2 = packet_info;  /* 0x00E17A6A `move.l (0x8,A6),(-0x34,A6)` */

    /* 0x00E17A74 `cmpi.l #0x1e,(0x18,A1)` is a SIGNED compare. */
    if ((int32_t)packet_info->data_len < XNS_IDP_HEADER_SIZE ||
        packet_info->header == 0) {                     /* 0x00E17A7E */
        *status_ret = status_$xns_error_illegal_buffer_spec;
        return;
    }

    orig = (const uint8_t *)ARCH_VA_TO_PTR(packet_info->header);   /* 0x00E17A92 */

    /*
     * 0x00E17A94 / 0x00E17AA2: the offending packet's IDP SOURCE address
     * (header +0x12) and DESTINATION address (header +0x06).  The two results
     * are OR-ed together (0x00E17AAC) and the sign bit of the result decides.
     */
    src_is_bcast = xns_$is_local_addr((void *)(orig + offsetof(xns_$idp_header_t, src_network)));
    if ((int8_t)(src_is_bcast |
                 xns_$is_local_addr((void *)(orig + offsetof(xns_$idp_header_t, dest_network)))) < 0) {
        *status_ret = status_$xns_error_source_is_broadcast;
        return;
    }

    /* 0x00E17AC2-0x00E17ACC: never answer an error packet with an error. */
    if ((uint16_t)orig[offsetof(xns_$idp_header_t, packet_type)] == XNS_IDP_TYPE_ERROR) {
        *status_ret = status_$xns_error_packet_type_error;
        return;
    }

    xns_$maybe_open_error_socket(&open_status);         /* 0x00E17AE0 */
    *status_ret = open_status;                          /* 0x00E17AEA */
    if (*status_ret != status_$ok) {                    /* 0x00E17AF2 */
        return;
    }

    socket_opened = true;                               /* 0x00E17AF8 `st` */

    /* 0x00E17AFC "pea (-0x28,A6)" then "pea (-0x2c,A6)": the handle is the
     * first argument, the VA the second. */
    NETBUF_$GET_HDR(&netbuf_handle, &frame.netbuf_va);  /* 0x00E17B04 */
    pkt = (xns_$error_pkt_t *)ARCH_VA_TO_PTR(frame.netbuf_va);  /* 0x00E17B0C */

    /*
     * 0x00E17B10-0x00E17B1E.  The first call classifies the packet's buffers
     * and its Domain boolean result lands in A6-0x3C; the second is the
     * nested procedure, which reads that slot to decide how to fetch the
     * bytes it appends to the error packet.
     */
    frame.in_netbuf_pool = xns_$pkt_bufs_in_netbuf_pool(packet_info);
    xns_$setup_error_header(&frame);

    /*
     * 0x00E17B22-0x00E17B2C: the length is 0x4C minus what
     * xns_$setup_error_header LEFT UNCOPIED, i.e. 0x22 plus the bytes it
     * appended.  A6-0x36 is a frame slot, not a field of the packet record.
     */
    packet_len = (int32_t)0x4C - (int32_t)frame.remaining;

    XNS_ERROR_$DATA.send_rec.hdr_desc.length  = packet_len;         /* 0x00E17B2C */
    XNS_ERROR_$DATA.send_rec.hdr_desc.address = frame.netbuf_va;    /* 0x00E17B30 */
    XNS_ERROR_$DATA.send_rec.hdr_desc.next    = 0;                  /* 0x00E17B34 */
    XNS_ERROR_$DATA.send_rec.hdr_prebuilt     = true;               /* 0x00E17B38 `st` */

    pkt->idp.checksum = 0xFFFF;                                     /* 0x00E17B3E */

    /* 0x00E17B42 "move.w (0x1a,A5),(0x2,A0)" - the LOW word of the longword
     * just stored at A5+0x18. */
    pkt->idp.length = (uint16_t)XNS_ERROR_$DATA.send_rec.hdr_desc.length;

    pkt->idp.transport_ctl = 0;                                     /* 0x00E17B48 */
    pkt->idp.packet_type   = XNS_IDP_TYPE_ERROR;                    /* 0x00E17B4C */

    /*
     * 0x00E17B52-0x00E17B5E: three longword moves from the NEW packet's +0x34
     * to its +0x06.  +0x34 is orig[0x12], i.e. the offending packet's IDP
     * source address as xns_$setup_error_header copied it in, so this is the
     * address swap: the error goes back to whoever sent the packet.
     */
    for (i = 0; i < 12; i++) {
        ((uint8_t *)pkt)[offsetof(xns_$idp_header_t, dest_network) + i] =
            pkt->orig[XNS_ERROR_ORIG_SRC_ADDR_OFFSET + i];
    }

    pkt->idp.src_socket  = XNS_SOCKET_ERROR;                        /* 0x00E17B60 */
    pkt->idp.src_network = 0;                                       /* 0x00E17B66 */

    /*
     * 0x00E17B6A-0x00E17B8E: the source host is the Apollo Ethernet address
     * 08:00:1E:0n:nn:nn built out of NODE_$ME - "move.w #0x800", then
     * ((NODE_$ME >> 16) & 0x0F) | 0x1E00, then the low word of NODE_$ME.
     * Written out as wire bytes so the host build agrees with the target.
     */
    {
        uint32_t node    = NODE_$ME;
        uint16_t host_w0 = 0x0800;
        uint16_t host_w1 = (uint16_t)(((node >> 16) & 0x0F) | 0x1E00);
        uint16_t host_w2 = (uint16_t)(node & 0xFFFF);

        pkt->idp.src_host[0] = (uint8_t)(host_w0 >> 8);
        pkt->idp.src_host[1] = (uint8_t)host_w0;
        pkt->idp.src_host[2] = (uint8_t)(host_w1 >> 8);
        pkt->idp.src_host[3] = (uint8_t)host_w1;
        pkt->idp.src_host[4] = (uint8_t)(host_w2 >> 8);
        pkt->idp.src_host[5] = (uint8_t)host_w2;
    }

    pkt->error_param = *error_param;    /* 0x00E17B96, argument 3 -> +0x20 */
    pkt->error_code  = *error_code;     /* 0x00E17B9E, argument 2 -> +0x1E */

    /* 0x00E17BA2-0x00E17BB0: "pea (0x76,A5)" is the channel cell, "pea (A5)"
     * the request record at the head of the module data. */
    XNS_IDP_$OS_SEND(&XNS_ERROR_$DATA.std_idp_channel,
                     &XNS_ERROR_$DATA.send_rec,
                     (int16_t *)result_ret, status_ret);

    xns_$maybe_close_error_socket();                    /* 0x00E17BBA */
    FIM_$RLS_CLEANUP(cleanup_buf);                      /* 0x00E17BC2 */
}
