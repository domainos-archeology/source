/*
 * PKT_$BRK_INTERNET_HDR - Take a Domain internet packet header apart
 *
 * The inverse of PKT_$BLD_INTERNET_HDR: it pulls the addressing, the request
 * id and the protocol pair out of a received pkt_$hdr_t and copies the
 * template that follows the header into the caller's buffer.
 *
 * Fourteen arguments; the one caller pops 0x34 bytes (0x00E68ACA).
 *
 * Original address: 0x00E12328 (358 bytes)
 */

#include "pkt/pkt_internal.h"

void PKT_$BRK_INTERNET_HDR(pkt_$hdr_t *hdr, uint16_t hdr_len,
                           uint32_t *routing_key, uint32_t *dest_node,
                           uint16_t *dest_sock, uint32_t *src_node_or,
                           uint32_t *src_node, uint16_t *src_sock,
                           uint16_t *info_out, uint16_t *id_out,
                           void *data_buf, uint16_t data_max,
                           uint16_t *data_len, status_$t *status_ret)
{
    uint16_t routing_type;      /* D1w */
    uint16_t copy_len;          /* D2w */
    uint32_t template_off;      /* D3 */

    /*
     * The second argument is never read: no instruction between 0x00E12328
     * and 0x00E1248C touches (0xC,A6).  It is in the signature because
     * RIP_$SERVER pushes sock_$pkt_info_t.hdr_len there (0x00E68ABE).
     */
    (void)hdr_len;

    /* 0x00E1234C - 0x00E1235E: both are byte fields widened to a word */
    info_out[0] = (uint16_t)hdr->flags_0e;
    info_out[1] = (uint16_t)hdr->routing_type;
    routing_type = info_out[1];

    *id_out = hdr->request_id;              /* 0x00E12366 */
    *data_len = hdr->template_len;          /* 0x00E1236E */
    *status_ret = status_$ok;               /* 0x00E12376 */

    /* 0x00E12378  cmpi.w #0x1,D1w */
    if (routing_type == PKT_ROUTING_LOCAL) {
        *src_node_or = 0;                   /* 0x00E12380 */
        *routing_key = 0;                   /* 0x00E12384 */
        *dest_node = NODE_$ME;              /* 0x00E12388 */

        /*
         * 0x00E12392 - 0x00E12398
         *   move.b (0x19,A2),D6b / add.w D6w,D6w
         *   move.w (0x1e,A2,D6w*0x1),(A3)
         * i.e. the word one past the last route word.
         */
        *dest_sock = hdr->u.route[hdr->route_count];

        /* 0x00E1239E  cmpi.w #0x4,D6w with D6 = hdr_size */
        if (hdr->hdr_size == PKT_HDR_SIZE_LOCAL) {
            *src_sock = hdr->src_sock;          /* 0x00E123A8 */
            *src_node = hdr->src_node;          /* 0x00E123B0 */
        } else {
            *src_sock = hdr->u.inet.src_sock;   /* 0x00E123B6 */
            *src_node = hdr->u.inet.src_node;   /* 0x00E123BE */
        }
    }

    /*
     * 0x00E123C2  cmpi.w #0x2,D1w - a second independent test, not an else:
     * a local header falls through this one with routing_type 1.
     */
    if (routing_type == PKT_ROUTING_INET) {
        *routing_key = hdr->u.inet.dest.net;                    /* 0x00E123CA */
        *dest_node   = hdr->u.inet.dest.node & 0x00FFFFFFu;     /* 0x00E123CE */
        *dest_sock   = hdr->u.inet.dest.sock;                   /* 0x00E123DE */
        *src_node_or = hdr->u.inet.src.net;                     /* 0x00E123E4 */
        *src_node    = hdr->u.inet.src.node & 0x00FFFFFFu;      /* 0x00E123E8 */
        *src_sock    = hdr->u.inet.src.sock;                    /* 0x00E123F8 */

        /* 0x00E123FC  clr.w D1w / move.b (0x2d,A2),D1b / cmpi.w #0x4,D1w */
        if ((uint16_t)hdr->u.inet.protocol == 4) {
            info_out[2] = 2;                            /* 0x00E12408 */
            info_out[3] = hdr->u.inet.subtype;          /* 0x00E1240E */

            /* 0x00E12416  cmpi.w #0x29,D6w */
            if (info_out[3] == PKT_SUBTYPE_LONG_ADDR) {
                int16_t i;
                uint8_t *dst = (uint8_t *)&info_out[7]; /* (0xe,A0), 0x00E12420 */

                /* 0x00E12424 - 0x00E1242A: moveq #0xf + dbf = 16 bytes */
                for (i = 0; i <= 0x0F; i++) {
                    dst[i] = hdr->u.inet.addr[i];
                }
            }
        } else {
            info_out[2] = 1;                            /* 0x00E12430 */
            info_out[3] = (uint16_t)hdr->u.inet.protocol; /* 0x00E12436 */
        }
    }

    /*
     * 0x00E1243A - 0x00E12458.  D1w = hdr_size + 0x1F is widened to a
     * longword and added to the template length before the compare.
     */
    template_off = (uint32_t)(uint16_t)(hdr->hdr_size + 0x1F);

    if (template_off + (uint32_t)*data_len >= PKT_MAX_HEADER) {
        *status_ret = status_$network_header_plus_data_too_big;   /* 0x00E1245E */
        return;
    }

    /* 0x00E12466  move.w (A4),D2w / cmp.w D0w,D2w / bls  (unsigned) */
    copy_len = *data_len;
    if (copy_len > data_max) {
        copy_len = data_max;
    }

    /*
     * 0x00E12472 - 0x00E1247C
     *   move.l D6,-(SP) / move.l (0x2e,A6),-(SP) / pea (-0x1,A2,D3*0x1)
     * The copy is unconditional; a zero length is handled by OS_$DATA_COPY.
     */
    OS_$DATA_COPY((char *)hdr + template_off - 1, (char *)data_buf,
                  (uint32_t)copy_len);

    *data_len = copy_len;       /* 0x00E12482 */
}
