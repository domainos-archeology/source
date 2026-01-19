/*
 * SOCK_$GET - Get next packet from socket receive queue
 *
 * Retrieves the next packet from a socket's receive queue and copies
 * the packet information to the provided buffer.
 *
 * The packet data is copied from the network buffer header (at high offsets
 * in the 1KB buffer) to the output pkt_info structure.
 *
 * Original address: 0x00E16070
 * Original source: Pascal, converted to C
 */

#include "sock_internal.h"

int8_t SOCK_$GET(uint16_t sock_num, void *pkt_info)
{
    sock_ec_view_t *sock_view;
    ml_$spin_token_t token;
    int8_t result;
    uint8_t *netbuf;
    uint32_t *out = (uint32_t *)pkt_info;
    int16_t i;
    uint16_t hop_count;

    /* Acquire spinlock */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* Get pointer to socket's EC view */
    sock_view = SOCK_GET_VIEW_PTR(sock_num);

    /* Check if queue is empty */
    if (sock_view->queue_count == 0) {
        /* No packets available */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);
        result = 0;
    } else {
        /* Decrement queue count */
        sock_view->queue_count--;

        /* Get pointer to first packet in queue */
        netbuf = (uint8_t *)sock_view->queue_head;

        /* Update queue head to next packet */
        sock_view->queue_head = *(uint32_t *)(netbuf + NETBUF_OFFSET_NEXT);

        /* If queue is now empty, clear tail pointer */
        if (sock_view->queue_head == 0) {
            sock_view->queue_tail = 0;
        }

        /* Release spinlock */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

        /*
         * Copy packet info from network buffer to output structure.
         * The layout mirrors the sock_pkt_info_t structure.
         */

        /* +0x04: Source address (from netbuf + 0x3BC) */
        out[1] = *(uint32_t *)(netbuf + NETBUF_OFFSET_SRC_ADDR);

        /* +0x08: Source port (from netbuf + 0x3C0) */
        *(uint16_t *)&out[2] = *(uint16_t *)(netbuf + NETBUF_OFFSET_SRC_PORT);

        /* +0x0C: Destination address (from netbuf + 0x3C4) */
        out[3] = *(uint32_t *)(netbuf + NETBUF_OFFSET_DST_ADDR);

        /* +0x10: Destination port (from netbuf + 0x3C8) */
        *(uint16_t *)&out[4] = *(uint16_t *)(netbuf + NETBUF_OFFSET_DST_PORT);

        /* +0x00: Header pointer (from netbuf + 0x3B8) */
        out[0] = *(uint32_t *)(netbuf + NETBUF_OFFSET_HDR_PTR);

        /* +0x2A: Data length (from netbuf + 0x3E8) */
        *(uint32_t *)((uint8_t *)out + 0x2A) = *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_LEN);

        /* +0x30: Data pointers (from netbuf + 0x3EC, 16 bytes) */
        out[0x0C] = *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_PTRS);
        out[0x0D] = *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_PTRS + 4);
        out[0x0E] = *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_PTRS + 8);
        out[0x0F] = *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_PTRS + 12);

        /* +0x12: Hop count (from netbuf + 0x3CA) */
        hop_count = *(uint16_t *)(netbuf + NETBUF_OFFSET_HOP_COUNT);
        *(uint16_t *)((uint8_t *)out + 0x12) = hop_count;

        /* +0x14: Hop array (from netbuf + 0x3CC, variable length) */
        if (hop_count > 0) {
            uint16_t *hop_out = (uint16_t *)((uint8_t *)out + 0x14);
            uint16_t *hop_in = (uint16_t *)(netbuf + NETBUF_OFFSET_HOP_ARRAY);

            for (i = hop_count - 1; i >= 0; i--) {
                *hop_out++ = *hop_in++;
            }
        }

        result = -1;  /* 0xFF = success */
    }

    return result;
}
