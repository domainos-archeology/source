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

#include "sock/sock_internal.h"

int8_t SOCK_$GET(uint16_t sock_num, void *pkt_info)
{
    sock_$sock_t *sock_view;
    ml_$spin_token_t token;
    int8_t result;
    uint8_t *netbuf;
    sock_$pkt_info_t *out = (sock_$pkt_info_t *)pkt_info;
    int16_t i;
    uint16_t hop_count;

    /* Acquire spinlock */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* Get pointer to socket's descriptor */
    sock_view = SOCK_GET_VIEW_PTR(sock_num);

    /* Check if queue is empty (0x00E160A6 move.b (0x15,A0),D1b) */
    if (sock_view->queue_count == 0) {
        /* No packets available */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);
        result = 0;
    } else {
        /* Decrement queue count (0x00E160C2 subq.b #1,(0x15,A0)) */
        sock_view->queue_count--;

        /* Get pointer to first packet in queue (0x00E160C6) */
        netbuf = (uint8_t *)(uintptr_t)sock_view->queue_head;

        /* Update queue head to next packet (0x00E160CA) */
        sock_view->queue_head = *(uint32_t *)(netbuf + NETBUF_OFFSET_NEXT);

        /* If queue is now empty, clear tail pointer (0x00E160D0) */
        if (sock_view->queue_head == 0) {
            sock_view->queue_tail = 0;
        }

        /* Release spinlock */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

        /*
         * Copy the packet header out of the netbuf into the caller's
         * sock_$pkt_info_t, in the original's order
         * (0x00E160EC-0x00E16142).
         */
        out->src_addr = *(uint32_t *)(netbuf + NETBUF_OFFSET_SRC_ADDR);
        out->src_port = *(uint16_t *)(netbuf + NETBUF_OFFSET_SRC_PORT);
        out->dst_addr = *(uint32_t *)(netbuf + NETBUF_OFFSET_DST_ADDR);
        out->flags    = *(uint16_t *)(netbuf + NETBUF_OFFSET_DST_PORT);
        out->hdr      = *(uint32_t *)(netbuf + NETBUF_OFFSET_HDR_PTR);

        /*
         * "move.l (0x3e8,A1),(0x2a,A2)" at 0x00E16108 copies the data length
         * and the header length together.
         */
        out->data_len = *(uint16_t *)(netbuf + NETBUF_OFFSET_DATA_LEN);
        out->hdr_len  = *(uint16_t *)(netbuf + NETBUF_OFFSET_DATA_LEN + 2);

        /* Four data page addresses (0x00E1610E-0x00E1611C) */
        for (i = 0; i < 4; i++) {
            out->data_pages[i] =
                *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_PTRS + i * 4);
        }

        /* Hop count (0x00E1611E) */
        hop_count = *(uint16_t *)(netbuf + NETBUF_OFFSET_HOP_COUNT);
        out->n_hops = hop_count;

        /*
         * Hop words (0x00E16124-0x00E1613E).  "subq.w #1,D0w / bmi" makes
         * this exactly hop_count iterations, so a hop count of zero copies
         * nothing.
         */
        {
            uint16_t *hop_out = out->hops;
            const uint16_t *hop_in =
                (const uint16_t *)(netbuf + NETBUF_OFFSET_HOP_ARRAY);

            for (i = (int16_t)hop_count - 1; i >= 0; i--) {
                *hop_out++ = *hop_in++;
            }
        }

        result = -1;  /* 0xFF = success */
    }

    return result;
}
