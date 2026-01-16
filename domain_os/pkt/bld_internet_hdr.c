/*
 * PKT_$BLD_INTERNET_HDR - Build an internet packet header
 *
 * Builds a complete internet packet header for network transmission.
 * Handles both local (loopback) and remote destinations.
 * Validates packet sizes and performs route lookup.
 *
 * The function handles three routing types:
 * - Type 1: Local/loopback (simple header)
 * - Type 2: Internet/remote (full internet header with routing)
 *
 * Original address: 0x00E1202C
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

/*
 * Maximum packet sizes
 */
#define PKT_MAX_LOCAL_DATA      0x1000  /* 4KB for local packets */
#define PKT_MAX_GATEWAY_DATA    0x400   /* 1KB for gateway packets */
#define PKT_MAX_DIRECT_DATA     0x100   /* Varies by port */
#define PKT_MAX_HEADER          0x3B8   /* Maximum header size */

void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           void *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len, uint16_t data_len,
                           int16_t *port_out, uint32_t *hdr_buf, uint16_t *len_out,
                           uint16_t *param15, uint16_t *param16, status_$t *status_ret)
{
    uint32_t effective_dest;
    int16_t route_result;
    uint8_t nexthop[6];
    int16_t routing_type;
    uint8_t hdr_size;
    uint16_t total_len;
    uint32_t local_addr_info[6];
    uint8_t *pkt_info_bytes = (uint8_t *)pkt_info;
    uint32_t *pkt_info_words = (uint32_t *)pkt_info;

    /* Check for loopback mode */
    if (NETWORK_$LOOPBACK_FLAG < 0) {
        effective_dest = NODE_$ME;
    } else {
        effective_dest = dest_node;
    }

    *status_ret = status_$ok;

    /* Copy basic header fields from pkt_info */
    /* Offset 0x04 in hdr_buf = byte from pkt_info offset 0x01 */
    ((uint8_t *)&hdr_buf[1])[0] = pkt_info_bytes[1];
    hdr_buf[1] &= 0xFF0000FF;  /* Clear middle bytes */
    ((uint8_t *)hdr_buf)[7] = 0;

    /* Set source node */
    hdr_buf[2] = NODE_$ME;

    /* Copy some fields */
    ((uint16_t *)hdr_buf)[14] = ((uint16_t *)&hdr_buf[5])[1];  /* hdr_buf[0x1C] */
    ((uint16_t *)hdr_buf)[13] = src_sock;  /* hdr_buf[0x1A] */

    /* Get routing type from pkt_info offset 0x02 */
    routing_type = *(int16_t *)(pkt_info_bytes + 2);

    if (routing_type == 1) {
        /*
         * Type 1: Local/loopback routing
         * Simple header format
         */
        hdr_buf[0] = effective_dest;
        ((uint8_t *)&hdr_buf[6])[0] = 4;  /* Header size = 4 */
        ((uint8_t *)hdr_buf)[0x19] = 1;   /* Routing type */
        ((uint16_t *)hdr_buf)[15] = (uint16_t)(effective_dest >> 16);  /* hdr_buf[0x1E] */
        ((uint16_t *)hdr_buf)[16] = dest_sock;  /* hdr_buf[0x20] */
        *port_out = 0;
        goto finish_header;
    }

    if (routing_type != 2) {
        /* Unknown routing type - skip to finish */
        goto finish_header;
    }

    /*
     * Type 2: Internet routing
     * Full header format with route lookup
     */

    /* Set up local address info for route lookup */
    local_addr_info[0] = routing_key;
    local_addr_info[1] = (local_addr_info[1] & 0xFFF00000) | (effective_dest & 0x000FFFFF);

    /* Find next hop */
    route_result = RIP_$FIND_NEXTHOP(local_addr_info, 0, port_out, nexthop, status_ret);

    if (*status_ret != status_$ok) {
        /* Keep destination from local_addr_info for error path */
        hdr_buf[0] = local_addr_info[1] & 0x000FFFFF;
        goto set_internet_fields;
    }

    /* Validate packet size based on route type */
    if (effective_dest == NODE_$ME) {
        /* Local destination */
        if (data_len > PKT_MAX_LOCAL_DATA) {
            *status_ret = status_$network_data_length_too_large;
        }
    } else if (route_result == 0) {
        /* Direct route - check port's MTU */
        /* TODO: Access ROUTE_$PORTP[*port_out] to get port info */
        /* For now, use simplified check */
        /* Port type check at offset 0x2C */
        /* MTU at offset 0x48->0x02 */
        if (data_len > PKT_MAX_DIRECT_DATA) {
            *status_ret = status_$network_data_length_too_large;
        }
        if ((uint32_t)data_len + (uint32_t)template_len > PKT_MAX_DIRECT_DATA + 0x100) {
            *status_ret = status_$network_msg_exceeds_max_size;
        }
    } else {
        /* Gateway route */
        if (data_len > PKT_MAX_GATEWAY_DATA) {
            *status_ret = status_$network_data_length_too_large;
        }
        if ((uint32_t)data_len + (uint32_t)template_len > 0x500) {
            *status_ret = status_$network_msg_exceeds_max_size;
        }
    }

    /* Set destination from route lookup result */
    hdr_buf[0] = local_addr_info[1] & 0x000FFFFF;

set_internet_fields:
    /* Build internet header fields */
    ((uint8_t *)&hdr_buf[6])[0] = 0x28;  /* Base header size = 40 */
    ((uint8_t *)hdr_buf)[0x19] = 4;       /* Routing type 4 for internet */
    ((uint16_t *)hdr_buf)[15] = src_sock; /* hdr_buf[0x1E] */

    hdr_buf[8] = NODE_$ME;                /* Source node */
    ((uint16_t *)hdr_buf)[18] = (uint16_t)(effective_dest >> 16);  /* hdr_buf[0x24] */
    ((uint16_t *)hdr_buf)[19] = dest_sock;  /* hdr_buf[0x26] */

    /* Protocol and length fields */
    ((uint16_t *)hdr_buf)[20] = pkt_info_words[3] & 0xFFFF;  /* hdr_buf[0x28] = pkt_info[0x0C] */
    ((uint16_t *)hdr_buf)[21] = template_len + 0x1E;         /* hdr_buf[0x2A] */

    ((uint8_t *)&hdr_buf[11])[0] = pkt_info_bytes[0x0B];    /* hdr_buf[0x2C] */
    ((uint8_t *)hdr_buf)[0x2D] = pkt_info_bytes[0x07];      /* Protocol byte */

    /* Routing key */
    *(uint32_t *)((char *)hdr_buf + 0x2E) = routing_key;

    /* Clear and set destination address */
    *(uint32_t *)((char *)hdr_buf + 0x32) &= 0xFF;
    hdr_buf[0x0D] &= 0xFF000000;
    hdr_buf[0x0D] |= dest_node;
    ((uint16_t *)hdr_buf)[28] = dest_sock;  /* hdr_buf[0x38] */

    /* Set source node - from parameter or from port */
    if (src_node_or == -1) {
        /* Use source from routing port */
        /* TODO: *(uint32_t *)((char *)hdr_buf + 0x3A) = ROUTE_$PORT[*port_out * 0x5C]; */
        *(uint32_t *)((char *)hdr_buf + 0x3A) = NODE_$ME;  /* Simplified */
    } else {
        *(uint32_t *)((char *)hdr_buf + 0x3A) = (uint32_t)src_node_or;
    }

    /* Clear and set source address */
    *(uint32_t *)((char *)hdr_buf + 0x3E) &= 0xFF;
    hdr_buf[0x10] &= 0xFF000000;
    hdr_buf[0x10] |= src_node;
    ((uint16_t *)hdr_buf)[34] = src_sock;  /* hdr_buf[0x44] */

    /* Check for extended header (type 2 in pkt_info offset 0x04) */
    if (*(int16_t *)(pkt_info_bytes + 4) == 2) {
        /* Extended internet header */
        *(uint32_t *)((char *)hdr_buf + 0x46) = (uint32_t)request_id;
        ((uint16_t *)hdr_buf)[37] = *(uint16_t *)(pkt_info_bytes + 6);  /* hdr_buf[0x4A] */
        ((uint8_t *)hdr_buf)[0x2D] = 4;  /* Extended protocol */
        ((uint8_t *)&hdr_buf[6])[0] += 6;  /* Add 6 to header size */

        /* Check for signature (0x29) */
        if (((uint16_t *)hdr_buf)[37] == 0x29) {
            /* Copy 16-byte signature from pkt_info offset 0x0E */
            int i;
            uint8_t *src = pkt_info_bytes + 0x0E;
            uint8_t *dst = (uint8_t *)&hdr_buf[0x13];
            for (i = 0; i < 16; i++) {
                *dst++ = *src++;
            }
            ((uint8_t *)&hdr_buf[6])[0] += 0x10;  /* Add 16 to header size */
        }
    }

finish_header:
    /* Copy common trailer fields */
    ((uint8_t *)&hdr_buf[3])[0] = pkt_info_bytes[3];  /* hdr_buf[0x0C] */
    ((uint8_t *)hdr_buf)[0x0D] = 0;
    ((uint8_t *)hdr_buf)[0x0F] = 0;
    ((uint8_t *)hdr_buf)[0x0E] = pkt_info_bytes[1];

    ((uint16_t *)hdr_buf)[9] = template_len;   /* hdr_buf[0x12] */
    ((uint16_t *)hdr_buf)[10] = data_len;      /* hdr_buf[0x14] */
    ((uint16_t *)hdr_buf)[11] = request_id;    /* hdr_buf[0x16] */

    /* Calculate total length */
    hdr_size = ((uint8_t *)&hdr_buf[6])[0];
    total_len = template_len + hdr_size + 0x1E;
    ((uint16_t *)hdr_buf)[8] = total_len;  /* hdr_buf[0x10] */

    if (total_len >= PKT_MAX_HEADER + 1) {
        *status_ret = status_$network_message_header_too_big;
        return;
    }

    *len_out = total_len;

    /* Copy template data if present */
    if ((int16_t)template_len > 0) {
        uint32_t template_offset = (uint32_t)(hdr_size + 0x1F);
        if (template_len + template_offset > PKT_MAX_HEADER) {
            *status_ret = 0x110024;  /* Header too big with template */
            return;
        }
        OS_$DATA_COPY((char *)template, (char *)hdr_buf + template_offset - 1, (uint32_t)template_len);
    }

    *param15 = 5;
    *param16 = 4;
}
