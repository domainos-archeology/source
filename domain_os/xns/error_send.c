/*
 * XNS Error Protocol Send Function
 *
 * Implementation of XNS_ERROR_$SEND for sending XNS Error Protocol packets.
 * The XNS Error Protocol is used to report undeliverable packets back to
 * the sender.
 *
 * Original address: 0x00E17A2E
 */

#include "xns/xns_internal.h"

/* Internal state for error socket */
int32_t XNS_ERROR_$STD_IDP_CHANNEL = 0;

/*
 * The request record XNS_ERROR_$SEND hands to XNS_IDP_$OS_SEND.  It is not a
 * local: A5 is 0x00E2B29C for this module and the call at 0x00E17BB0 passes
 * "pea (A5)", so the record IS the module's data base.  Only +0x18..+0x24 is
 * written (0x00E17B2C-0x00E17B38); the address block at +0x00..+0x17 is
 * never touched because the error channel builds its own IDP header.
 */
static xns_$os_send_rec_t xns_error_send_params;

/*
 * Static helper: xns_$maybe_open_error_socket
 *
 * Opens the error protocol socket if not already open.
 *
 * Original address: 0x00E178AA
 */
static void xns_$maybe_open_error_socket(status_$t *status_ret)
{
    if (XNS_ERROR_$STD_IDP_CHANNEL == 0) {
        struct {
            int16_t socket;
            int16_t channel_ret;
            code_ptr_t demux_callback;
            void *user_data;
        } open_opt;

        open_opt.socket = XNS_SOCKET_ERROR;
        open_opt.demux_callback = NULL;
        open_opt.user_data = NULL;

        XNS_IDP_$OS_OPEN(&open_opt, status_ret);
        if (*status_ret == status_$ok) {
            XNS_ERROR_$STD_IDP_CHANNEL = open_opt.channel_ret;
        }
    } else {
        *status_ret = status_$ok;
    }
}

/*
 * Static helper: xns_$maybe_close_error_socket
 *
 * Closes the error protocol socket if open.
 *
 * Original address: 0x00E17910
 */
static void xns_$maybe_close_error_socket(void)
{
    if (XNS_ERROR_$STD_IDP_CHANNEL != 0) {
        status_$t status;
        int16_t channel = XNS_ERROR_$STD_IDP_CHANNEL;
        XNS_IDP_$OS_CLOSE(&channel, &status);
        XNS_ERROR_$STD_IDP_CHANNEL = 0;
    }
}

/*
 * The directly-addressable window for buffer-descriptor addresses, at the
 * module data base + 0x6C and + 0x70 (0x00E2B308 / 0x00E2B30C).  Neither cell
 * has any other reference in the image; both hold their initial values, so
 * the window is [0x00D64C00, 0x00D94C00) -- the network buffer pool.
 * xns_$pkt_bufs_in_netbuf_pool compares against them with SIGNED longword
 * compares (0x00E1788C `cmp.l (0x70,A5),D1` / `blt`, 0x00E17892
 * `cmp.l (0x6c,A5),D1` / `blt`), so the C keeps them signed too.
 */
int32_t XNS_ERROR_$BUF_VA_LOW  = 0x00D64C00;    /* 0x00E2B30C, A5+0x70 */
int32_t XNS_ERROR_$BUF_VA_HIGH = 0x00D94C00;    /* 0x00E2B308, A5+0x6C */

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
 * The three fields are lifted out by name rather than by aliasing the record,
 * because xns_$pkt_desc_t.header is declared as a C pointer: the record is
 * 0x48 bytes only on a 32-bit target, so an alias would read the wrong bytes
 * on the host (bead source-ronb).
 */
static mac_os_$buf_desc_t xns_$pkt_head_desc(const xns_$pkt_desc_t *packet_info)
{
    mac_os_$buf_desc_t head;

    head.length  = (int32_t)packet_info->data_len;
    head.address = ARCH_PTR_TO_VA(packet_info->header);
    head.next    = packet_info->iov;
    return head;
}

/*
 * Static helper: xns_$pkt_bufs_in_netbuf_pool (0x00E17876)
 *
 * Walks the received packet's buffer-descriptor chain and returns TRUE only
 * if EVERY descriptor's address lies in the network-buffer window
 * [XNS_ERROR_$BUF_VA_LOW, XNS_ERROR_$BUF_VA_HIGH).  It copies nothing; the
 * tree used to carry it under the name xns_$copy_header with a body that
 * returned packet_info[0x2D] (bead source-mck5).
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

        if (va < XNS_ERROR_$BUF_VA_LOW) {       /* 0x00E1788C / `blt` */
            all_in_pool = false;                /* 0x00E17898 `clr.b D0b` */
            break;
        }
        if (va >= XNS_ERROR_$BUF_VA_HIGH) {     /* 0x00E17892 / `blt` to next */
            all_in_pool = false;
            break;
        }
        node = (const mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(node->next);
    }

    return all_in_pool;
}

/*
 * Static helper: xns_$setup_error_header (0x00E17960)
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
 * XNS_ERROR_$SEND - Send an XNS Error Protocol packet
 *
 * Sends an error response packet for a received packet that could
 * not be processed. Error packets contain:
 *   - The first 42 bytes of the original packet (IDP header + 12 data bytes)
 *   - Error code and parameter
 *
 * Error packet format (after IDP header):
 *   +0x1E: Error code (2 bytes)
 *   +0x20: Error parameter (2 bytes)
 *   +0x22: Original packet data (42 bytes minimum)
 *
 * @param packet_info   Original packet information structure:
 *                      +0x18: header length
 *                      +0x1C: header pointer
 * @param error_code    Pointer to error code
 * @param error_param   Pointer to error parameter
 * @param result_ret    Output: unused result
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17A2E
 */
void XNS_ERROR_$SEND(void *packet_info, uint16_t *error_code, uint16_t *error_param,
                     uint16_t *result_ret, status_$t *status_ret)
{
    uint8_t *pkt = (uint8_t *)packet_info;
    int32_t header_len;
    int16_t *orig_header;
    int16_t *error_header;
    uint32_t netbuf_handle = 0;     /* A6-0x2c: header buffer physical address
                                     *          (clr.l (-0x2c,A6) @0xE17A3C) */
    uint8_t cleanup_buf[24];
    status_$t local_status;
    int16_t packet_offset;
    xns_$error_send_frame_t frame;  /* the slots the nested procedure reads */

    *result_ret = 0;
    *status_ret = status_$ok;

    /* 0x00E17A6A `move.l (0x8,A6),(-0x34,A6)` happens further down, but the
     * nested procedure only ever runs after that point. */
    frame.packet_info   = (xns_$pkt_desc_t *)packet_info;
    frame.packet_info_2 = (xns_$pkt_desc_t *)packet_info;
    frame.status_ret    = status_ret;

    /* Set up cleanup handler */
    local_status = FIM_$CLEANUP(cleanup_buf);
    if (local_status != status_$cleanup_handler_set) {
        /* Cleanup failed - return original packet if allocated */
        if (netbuf_handle != 0) {
            NETBUF_$RTN_HDR(&frame.netbuf_va);   /* 0x00E17BD0 */
        }
        *status_ret = local_status;
        return;
    }

    /* Validate original packet */
    header_len = *(int32_t *)(pkt + 0x18);
    orig_header = *(int16_t **)(pkt + 0x1C);

    if (header_len < XNS_IDP_HEADER_SIZE || orig_header == NULL) {
        *status_ret = status_$xns_illegal_buffer_spec;
        return;
    }

    /* Check source address isn't broadcast */
    {
        int8_t src_bc = xns_$is_local_addr((uint8_t *)orig_header + 0x12);
        int8_t dest_bc = xns_$is_local_addr((uint8_t *)orig_header + 0x06);

        if ((src_bc | dest_bc) < 0) {
            *status_ret = status_$xns_illegal_buffer_spec;
            return;
        }
    }

    /* Don't send error for error packets */
    if (*(uint8_t *)((uint8_t *)orig_header + 5) == XNS_IDP_TYPE_ERROR) {
        *status_ret = status_$xns_illegal_buffer_spec;
        return;
    }

    /* Open error socket if needed */
    xns_$maybe_open_error_socket(&local_status);
    *status_ret = local_status;
    if (*status_ret != status_$ok) {
        return;
    }

    /* Get a network buffer for the error packet */
    /* 0x00E17AFC: pea (-0x28,A6) then pea (-0x2c,A6) - phys first, VA second */
    NETBUF_$GET_HDR(&netbuf_handle, &frame.netbuf_va);
    error_header = (int16_t *)ARCH_VA_TO_PTR(frame.netbuf_va); /* movea.l (-0x28,A6),A2 */

    /*
     * 0x00E17B10-0x00E17B1E.  The first call classifies the packet's buffers
     * and its Domain boolean result lands in A6-0x3C; the second is the
     * nested procedure, which reads that slot to decide how to fetch the
     * bytes it appends to the error packet.
     */
    frame.in_netbuf_pool =
        xns_$pkt_bufs_in_netbuf_pool((const xns_$pkt_desc_t *)packet_info);
    xns_$setup_error_header(&frame);

    /*
     * 0x00E17B22-0x00E17B2C: the length is 0x4C minus what
     * xns_$setup_error_header LEFT UNCOPIED, i.e. 0x22 plus the bytes it
     * appended.  A6-0x36 is a frame slot, not a field of the packet record.
     */
    packet_offset = (int16_t)(0x4C - frame.remaining);

    xns_error_send_params.hdr_desc.length  = packet_offset;   /* 0x00E17B2C */
    xns_error_send_params.hdr_desc.address = frame.netbuf_va; /* 0x00E17B30 */
    xns_error_send_params.hdr_desc.next    = 0;               /* 0x00E17B34 */
    xns_error_send_params.hdr_prebuilt     = true;            /* 0x00E17B38 `st' */

    /* Set checksum to "compute" */
    error_header[0] = -1;

    /* Set length */
    error_header[1] = (uint16_t)xns_error_send_params.hdr_desc.length;

    /* Set transport control and packet type */
    *(uint8_t *)((uint8_t *)error_header + 4) = 0;
    *(uint8_t *)((uint8_t *)error_header + 5) = XNS_IDP_TYPE_ERROR;

    /* Swap source and destination - destination becomes original source */
    *(uint32_t *)(error_header + 3) = *(uint32_t *)(orig_header + 0x1A / 2);
    *(uint32_t *)(error_header + 5) = *(uint32_t *)(orig_header + 0x1C / 2);
    *(uint32_t *)(error_header + 7) = *(uint32_t *)(orig_header + 0x1E / 2);

    /* Set source address - use error socket (port 3) */
    error_header[0x0E / 2] = XNS_SOCKET_ERROR;

    /* Clear source network/host (will be filled by send) */
    *(uint32_t *)(error_header + 9) = 0;
    error_header[0x0B] = 0x800;

    /* Extract local node info for source */
    {
        uint32_t node = NODE_$ME;
        uint16_t host_hi = ((node >> 16) & 0x0F) | 0x1E00;
        uint16_t host_lo = node & 0xFFFF;
        error_header[0x0C] = host_hi;
        error_header[0x0D] = host_lo;
    }

    /* Set error code and parameter */
    error_header[0x10 / 2] = *error_code;
    error_header[0x0F] = *error_param;

    /* Send the error packet */
    /* 0x00E17BA2-0x00E17BB0: arg1 is (0x76,A5), arg2 the record at (A5) */
    XNS_IDP_$OS_SEND((int16_t *)&XNS_ERROR_$STD_IDP_CHANNEL,
                     &xns_error_send_params, (int16_t *)result_ret, status_ret);

    /* Close error socket */
    xns_$maybe_close_error_socket();

    /* Release cleanup handler */
    FIM_$RLS_CLEANUP(cleanup_buf);
}
