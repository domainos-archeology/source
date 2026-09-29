/*
 * PKT_$BLD_INTERNET_HDR - Build a Domain internet packet header
 *
 * Fills in the pkt_$hdr_t a caller obtained from NETBUF_$GET_HDR, copies the
 * caller's template in behind the header, and reports the total length, the
 * port to send on, the retry limit and the response timeout.
 *
 * Seventeen arguments plus a 2-byte Pascal function-result slot; both callers
 * pop 0x3C bytes (0x00E12712 in PKT_$SEND_INTERNET, 0x00E0DA98 in MSG_$$SEND).
 * The result slot is never written, so the routine is a Pascal *procedure*
 * whose callers reserved a slot they do not read.
 *
 * Original address: 0x00E1202C (764 bytes)
 */

#include "pkt/pkt_internal.h"

void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           const pkt_$info_t *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len, uint16_t data_len,
                           int16_t *port_out, pkt_$hdr_t *hdr, uint16_t *len_out,
                           uint16_t *retry_hint, uint16_t *timeout_out,
                           status_$t *status_ret)
{
    uint32_t effective_dest;    /* A6-0x30 */
    uint32_t nexthop_node;      /* D1 */
    int16_t  rip_result;        /* D0 */
    uint8_t  hdr_size;          /* D0w, reloaded from the header at 0x00E122AE */
    uint16_t total_len;         /* D1w */

    /*
     * A6-0x20 and A6-0x10.  The destination record is only PARTLY
     * initialised (see below), so it is deliberately left uninitialised here.
     */
    rip_$dest_addr_t dest_addr;
    rip_$nexthop_t   nexthop;

    /*
     * 0x00E1204C  tst.b (0x00e24c44).l / bpl
     * A Domain boolean: 0xFF means "loop every packet back to this node".
     */
    if (NETWORK_$LOOPBACK_FLAG < 0) {
        effective_dest = NODE_$ME;      /* 0x00E12054 */
    } else {
        effective_dest = dest_node;     /* 0x00E1205E */
    }

    *status_ret = status_$ok;           /* 0x00E12068 clr.l (A0) */

    /*
     * Common prologue, 0x00E1206A - 0x00E1208A.
     *
     *   move.b (0x1,A3),(0x4,A2)          info_flags = low byte of info.flags
     *   andi.l #-0xffff01,(0x4,A2)        keep +0x04 and +0x07, clear +0x05/+0x06
     *   clr.b (0x7,A2)                    ... and then +0x07 as well
     *   move.l NODE_$ME,(0x8,A2)
     *   move.w (0xa,A2),(0x1c,A2)         src_node_lo = low word of NODE_$ME
     *   move.w D4w,(0x1a,A2)
     */
    hdr->info_flags = (uint8_t)pkt_info->flags;
    hdr->zero_05[0] = 0;
    hdr->zero_05[1] = 0;
    hdr->zero_05[2] = 0;
    hdr->src_node = NODE_$ME;
    hdr->src_node_lo = (uint16_t)(hdr->src_node & 0xFFFFu);
    hdr->src_sock = src_sock;

    /* 0x00E1208E  cmpi.w #0x2,(0x2,A3) / bne 0x00e12262 */
    if (pkt_info->routing_type == PKT_ROUTING_INET) {
        /*
         * 0x00E12098 - 0x00E120AA: build the RIP destination.  Only the
         * network and the node are filled in; the node's top 12 bits are
         * whatever the stack happened to hold ("andi.l #-0x100000,(-0x1a,A6)"
         * masks the UNINITIALISED slot before or-ing the node in).  Every
         * consumer masks the node down again, so the stale bits are harmless
         * - but they are in the original and are reproduced here.
         */
        dest_addr.network = routing_key;
        dest_addr.host_lo = (dest_addr.host_lo & 0xFFF00000u) | effective_dest;

        /* 0x00E120AE - 0x00E120C6, the word result comes back in D0 */
        rip_result = RIP_$FIND_NEXTHOP(&dest_addr, 0, port_out, &nexthop, status_ret);

        /* 0x00E120CA  move.l #0xfffff,D1 / and.l (-0xa,A6),D1 */
        nexthop_node = nexthop.host_lo & 0x000FFFFFu;

        /* 0x00E120D8  tst.l (A0) / bne 0x00e1217c */
        if (*status_ret == status_$ok) {
            if (effective_dest == NODE_$ME) {
                /* 0x00E120E2 - 0x00E120F2 */
                if (data_len > 0x1000) {
                    *status_ret = status_$network_data_length_too_large;
                }
            } else if (rip_result == 0) {
                /*
                 * Directly attached port, 0x00E120F8 - 0x00E12148.
                 *
                 *   move.w (A1),D7w / lsl.w #0x2,D7w / lea (0x0,A4,D7w*0x1),A4
                 * indexes ROUTE_$PORTP with the port number the RIP lookup
                 * just stored, in WORD arithmetic.
                 */
                route_$port_t *port = ROUTE_$WIRED_DATA.portp[*port_out];
                int16_t port_state = (int16_t)port->active;

                if (port_state == 1 || port_state == 0) {
                    /* 0x00E1214A */
                    *status_ret = status_$network_request_denied_by_local_node;
                } else {
                    const route_$driver_info_t *drv =
                        (const route_$driver_info_t *)ARCH_VA_TO_PTR(port->driver_info);

                    /* 0x00E1211E  cmp.w (0x2,A0),D3w / bls  (unsigned) */
                    if (data_len > drv->max_data_len) {
                        *status_ret = status_$network_data_length_too_large;
                    } else if ((uint32_t)template_len + (uint32_t)data_len >
                               (uint32_t)drv->max_data_len + 0x100u) {
                        /* 0x00E1212A - 0x00E12148 */
                        *status_ret = status_$network_msg_exceeds_max_size;
                    }
                }
            } else {
                /* Routed through a gateway, 0x00E12156 - 0x00E12176 */
                if (data_len > 0x400) {
                    *status_ret = status_$network_data_length_too_large;
                } else if ((uint32_t)template_len + (uint32_t)data_len > 0x500u) {
                    *status_ret = status_$network_msg_exceeds_max_size;
                }
            }
        }

        /*
         * 0x00E1217C onwards runs on EVERY internet path, including the ones
         * that just set a status: the error branches all jump here.
         */
        hdr->dest_node = nexthop_node;                          /* 0x00E1217C */
        hdr->hdr_size = PKT_HDR_SIZE_INET;                      /* 0x00E1217E */
        hdr->route_count = 4;                                   /* 0x00E12184 */
        hdr->u.inet.src_sock = src_sock;                        /* 0x00E1218A */
        hdr->u.inet.src_node = NODE_$ME;                        /* 0x00E1218E */
        hdr->u.inet.dest_node_lo = (uint16_t)(effective_dest & 0xFFFFu); /* 0x00E12196 */
        hdr->u.inet.dest_sock = dest_sock;                      /* 0x00E1219C */
        hdr->u.inet.info_0c = pkt_info->field_0c;               /* 0x00E121A0 */
        hdr->u.inet.tpl_len_x = (uint16_t)(template_len + 0x1E);/* 0x00E121A6 */
        hdr->u.inet.info_0b = (uint8_t)pkt_info->field_0a;      /* 0x00E121AE */
        hdr->u.inet.protocol = (uint8_t)pkt_info->protocol;     /* 0x00E121B4 */

        /*
         * 0x00E121BA - 0x00E121D8.  The two andi.l's between them clear
         * hdr+0x32..0x37, so the or.l is a plain store of the node.
         */
        hdr->u.inet.dest.net = routing_key;
        hdr->u.inet.dest.zero = 0;
        hdr->u.inet.dest.node = dest_node;
        hdr->u.inet.dest.sock = dest_sock;                      /* 0x00E121D8 */

        /* 0x00E121DC  cmpi.l #-0x1,(0x12,A6) */
        if (src_node_or == -1) {
            /*
             * 0x00E121E6 - 0x00E121F4
             *   moveq #0x5c,D7 / mulu.w (A1),D7 / move.l (0x0,A0,D7w*0x1),...
             * i.e. ROUTE_$PORT_ARRAY[*port_out].network.
             */
            hdr->u.inet.src.net = ROUTE_$PORT_ARRAY[*port_out].network;
        } else {
            hdr->u.inet.src.net = (uint32_t)src_node_or;        /* 0x00E121FC */
        }

        /* 0x00E12202 - 0x00E1221A, same shape as the destination above */
        hdr->u.inet.src.zero = 0;
        hdr->u.inet.src.node = src_node;
        hdr->u.inet.src.sock = src_sock;

        /* 0x00E1221E  cmpi.w #0x2,(0x4,A3) */
        if (pkt_info->addr_type == 2) {
            hdr->u.inet.long_request_id = (uint32_t)request_id;  /* 0x00E12226 */
            hdr->u.inet.subtype = pkt_info->protocol;            /* 0x00E12230 */
            hdr->u.inet.protocol = 4;                            /* 0x00E12236 */
            hdr->hdr_size = (uint8_t)(hdr->hdr_size + PKT_HDR_SIZE_LONG_ID); /* 0x00E1223C */

            /* 0x00E12240  cmpi.w #0x29,(0x4a,A2) */
            if (hdr->u.inet.subtype == PKT_SUBTYPE_LONG_ADDR) {
                int16_t i;

                /* 0x00E12248 - 0x00E12256: moveq #0xf + dbf = 16 bytes */
                for (i = 0; i <= 0x0F; i++) {
                    hdr->u.inet.addr[i] = pkt_info->addr[i];
                }
                /* 0x00E1225A  moveq #0x10,D0 / add.b D0b,(0x18,A2) */
                hdr->hdr_size = (uint8_t)(hdr->hdr_size + PKT_HDR_SIZE_ADDR);
            }
        }
    } else if (pkt_info->routing_type == PKT_ROUTING_LOCAL) {
        /* 0x00E12262 - 0x00E12288 */
        hdr->dest_node = effective_dest;
        hdr->hdr_size = PKT_HDR_SIZE_LOCAL;
        hdr->route_count = 1;
        hdr->u.local.dest_node_lo = (uint16_t)(effective_dest & 0xFFFFu);
        hdr->u.local.dest_sock = dest_sock;
        *port_out = 0;
    }
    /*
     * Any other routing_type falls straight through to the common tail with
     * hdr_size / route_count / dest_node left as the caller supplied them
     * ("bne.b 0x00e1228a" at 0x00E12268).
     */

    /* Common tail, 0x00E1228A - 0x00E122BA */
    hdr->routing_type = (uint8_t)pkt_info->routing_type;
    hdr->zero_0d = 0;
    hdr->zero_0f = 0;
    hdr->flags_0e = (uint8_t)pkt_info->flags;
    hdr->template_len = template_len;
    hdr->data_len = data_len;
    hdr->request_id = request_id;

    hdr_size = hdr->hdr_size;
    total_len = (uint16_t)(hdr_size + template_len + PKT_HDR_FIXED_LEN);
    hdr->total_len = total_len;

    /* 0x00E122BE  cmpi.w #0x3b8,D1w / bls  (unsigned) */
    if (total_len > PKT_MAX_HEADER) {
        *status_ret = status_$network_msg_header_too_big;
        return;
    }

    *len_out = total_len;                       /* 0x00E122D4 */

    /* 0x00E122D6  tst.w D2w / ble  (SIGNED) */
    if ((int16_t)template_len > 0) {
        /* D0w = hdr_size + 0x1F, then a 32-bit compare (0x00E122DA-0x00E122F0) */
        uint32_t template_off = (uint32_t)(uint16_t)(hdr_size + 0x1F);

        if (template_off + (uint32_t)template_len >= PKT_MAX_HEADER) {
            *status_ret = status_$network_header_data_length_exceeds_max;
            return;
        }

        /*
         * 0x00E122FE - 0x00E12308
         *   move.l D4,-(SP) / pea (-0x1,A2,D3*0x1) / move.l (0x22,A6),-(SP)
         * so the destination is hdr + hdr_size + 0x1F - 1.
         */
        OS_$DATA_COPY((char *)template,
                      (char *)hdr + template_off - 1,
                      (uint32_t)template_len);
    }

    *retry_hint = 5;        /* 0x00E1230E  move.w #0x5,(A0) */
    *timeout_out = 4;       /* 0x00E1231A  move.w #0x4,(A1) */
}
