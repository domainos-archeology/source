/*
 * PKT_$BRK_INTERNET_HDR - Break down (parse) an internet packet header
 *
 * Parses a received internet packet header and extracts addressing
 * and protocol information. This is the inverse of PKT_$BLD_INTERNET_HDR.
 *
 * The function handles different routing types:
 * - Type 1: Local/loopback (simple header)
 * - Type 2: Internet/remote (full internet header)
 *
 * Original address: 0x00E12328
 */

#include "pkt/pkt_internal.h"

void PKT_$BRK_INTERNET_HDR(void *hdr_ptr, uint32_t *routing_key, uint32_t *dest_node,
                           uint16_t *dest_sock, uint32_t *src_node_or, uint32_t *src_node,
                           uint16_t *src_sock, uint16_t *info_out, uint16_t *id_out,
                           char *template_out, uint16_t template_max,
                           uint16_t *template_len_out, status_$t *status_ret)
{
    uint8_t *hdr = (uint8_t *)hdr_ptr;
    uint16_t routing_type;
    uint8_t hdr_size;
    uint16_t template_len;
    uint16_t copy_len;
    uint32_t template_offset;
    uint8_t protocol_byte;

    /* Extract basic info from header */
    /* info_out[0] = header byte at offset 0x0E (flags) */
    info_out[0] = (uint16_t)hdr[0x0E];
    /* info_out[1] = header byte at offset 0x0C (type) */
    info_out[1] = (uint16_t)hdr[0x0C];
    routing_type = info_out[1];

    /* Extract request ID */
    *id_out = *(uint16_t *)(hdr + 0x16);

    /* Extract template length */
    *template_len_out = *(uint16_t *)(hdr + 0x12);

    *status_ret = status_$ok;

    if (routing_type == 1) {
        /*
         * Type 1: Local/loopback header
         */
        *src_node_or = 0;
        *routing_key = 0;
        *dest_node = NODE_$ME;

        /* Calculate address offset based on header size indicator */
        hdr_size = hdr[0x19];
        *dest_sock = *(uint16_t *)(hdr + 0x1E + hdr_size * 2);

        if (hdr[0x18] == 4) {
            /* Simple header format */
            *src_sock = *(uint16_t *)(hdr + 0x1A);
            *src_node = *(uint32_t *)(hdr + 0x08);
        } else {
            /* Extended header format */
            *src_sock = *(uint16_t *)(hdr + 0x1E);
            *src_node = *(uint32_t *)(hdr + 0x20);
        }
    }

    if (routing_type == 2) {
        /*
         * Type 2: Internet header
         */
        *routing_key = *(uint32_t *)(hdr + 0x2E);
        *dest_node = *(uint32_t *)(hdr + 0x34) & 0x00FFFFFF;  /* 24-bit node ID */
        *dest_sock = *(uint16_t *)(hdr + 0x38);
        *src_node_or = *(uint32_t *)(hdr + 0x3A);
        *src_node = *(uint32_t *)(hdr + 0x40) & 0x00FFFFFF;   /* 24-bit node ID */
        *src_sock = *(uint16_t *)(hdr + 0x44);

        /* Check protocol byte */
        protocol_byte = hdr[0x2D];

        if (protocol_byte == 4) {
            /* Extended protocol - has additional fields */
            info_out[2] = 2;  /* Extended type indicator */
            info_out[3] = *(uint16_t *)(hdr + 0x4A);

            /* Check for signature (0x29) */
            if (info_out[3] == 0x29) {
                /* Copy 16-byte signature to info_out[7..] */
                int i;
                uint8_t *src = hdr + 0x4C;
                uint8_t *dst = (uint8_t *)&info_out[7];
                for (i = 0; i < 16; i++) {
                    *dst++ = *src++;
                }
            }
        } else {
            /* Standard protocol */
            info_out[2] = 1;
            info_out[3] = (uint16_t)protocol_byte;
        }
    }

    /* Copy template data */
    hdr_size = hdr[0x18];
    template_offset = (uint32_t)(hdr_size + 0x1F);
    template_len = *template_len_out;

    if (template_offset + template_len >= PKT_MAX_HEADER) {
        *status_ret = 0x110024;  /* Header/template too big */
        return;
    }

    /* Determine how much to copy */
    copy_len = template_len;
    if (copy_len > template_max) {
        copy_len = template_max;
    }

    /* Copy template data */
    OS_$DATA_COPY((char *)(hdr + template_offset - 1), template_out, (uint32_t)copy_len);

    *template_len_out = copy_len;
}
