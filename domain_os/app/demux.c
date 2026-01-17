/*
 * APP_$DEMUX - Demultiplex received packet
 *
 * Routes a received packet to the appropriate handler based on the
 * socket type. Called by XNS IDP layer when a packet arrives.
 *
 * Original address: 0x00E00A90
 */

#include "app/app_internal.h"

void APP_$DEMUX(void *pkt_info, uint16_t *ec_ptr1, uint16_t *ec_ptr2,
                int8_t *flags, status_$t *status_ret)
{
    uint32_t *info_ptr = (uint32_t *)pkt_info;
    uint8_t *app_hdr;
    uint32_t pkt_ptr;
    uint16_t sock_num;
    int8_t put_result;

    /* Local stack structure */
    uint32_t local_pkt;
    uint32_t local_routing_key;
    uint16_t local_sock;
    uint16_t local_demux_flags;
    uint16_t local_unused;
    uint16_t local_port;
    uint16_t local_len;

    /* Data buffers for UID info (16 bytes) */
    uint32_t data_bufs[4];

    *status_ret = status_$ok;

    /* Get packet pointer from info structure */
    pkt_ptr = info_ptr[7];  /* Offset 0x1C */
    local_pkt = pkt_ptr;

    /* Calculate APP header location (offset 0x1E from packet) */
    app_hdr = (uint8_t *)(pkt_ptr + 0x1E);

    /*
     * Check if this is a special packet type that should be handled directly:
     * - Network type 2 (remote)
     * - Socket type 4
     * - Flag at offset 0x14 is negative (bit 7 set)
     * - And caller flags is non-negative
     */
    if (*(uint32_t *)(app_hdr + 0x08) == 2 &&
        *(int16_t *)(app_hdr + 0x0C) == 4 &&
        *(int8_t *)(app_hdr + 0x14) < 0 &&
        *flags >= 0) {
        /* Direct handling - skip to buffer return */
        goto return_buffers;
    }

    /* Set up demux flags */
    local_demux_flags = 2;  /* Default flags */
    if (*flags < 0) {
        local_demux_flags = 6;  /* Set overflow bit */
    }

    /* Copy routing/socket info from packet info structure */
    local_routing_key = info_ptr[9];    /* Offset 0x26 (actually 0x24 + 2 for alignment) */
    local_sock = *(uint16_t *)((uint8_t *)pkt_info + 0x2A);
    local_port = *(uint16_t *)((uint8_t *)pkt_info + 0x1A);
    local_len = *(uint16_t *)((uint8_t *)pkt_info + 0x36);

    /* Copy data buffer info (16 bytes at offset 0x38) */
    data_bufs[0] = info_ptr[14];  /* 0x38 */
    data_bufs[1] = info_ptr[15];  /* 0x3C */
    data_bufs[2] = info_ptr[16];  /* 0x40 */
    data_bufs[3] = info_ptr[17];  /* 0x44 */

    local_unused = 0;

    /* Get socket number from app header */
    sock_num = *(uint16_t *)(app_hdr + 0x0C);

    /* Try to put packet on target socket */
    put_result = SOCK_$PUT(sock_num, (void **)&local_pkt, 0, *ec_ptr1, *ec_ptr2);

    if (put_result >= 0) {
        /* Socket full or error - check if we should try overflow */
        if (sock_num == APP_SOCK_TYPE_FILE) {
            /* File socket overflow - try overflow socket */
            RING_$FILE_OVERFLOW++;

            put_result = SOCK_$PUT(APP_SOCK_TYPE_OVERFLOW, (void **)&local_pkt, 0,
                                   *ec_ptr1, *ec_ptr2);

            if (put_result >= 0) {
                /* Overflow socket also full */
                RING_$OVERFLOW_OVERFLOW++;
                goto return_buffers;
            }
        }

        /* Packet successfully queued to overflow or non-file socket */
        return;
    }

    /* Packet successfully queued to target socket */
    return;

return_buffers:
    /* Return header buffer to pool */
    NETBUF_$RTN_HDR(&local_pkt);

    /* Return data buffers if present */
    if (data_bufs[0] != 0) {
        PKT_$DUMP_DATA(data_bufs, local_len);
    }
}
