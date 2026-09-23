/*
 * SOCK_$PUT - Put packet on socket receive queue
 *
 * This file contains the three levels of PUT functions:
 * - SOCK_$PUT: High-level interface, advances event count on success
 * - SOCK_$PUT_INT: Mid-level, validates socket and returns EC pointer
 * - SOCK_$PUT_INT_INT: Low-level, performs actual queue insertion
 *
 * Original addresses:
 *   SOCK_$PUT:         0x00E1614E
 *   SOCK_$PUT_INT:     0x00E16190
 *   SOCK_$PUT_INT_INT: 0x00E161F8
 *
 * Original source: Pascal, converted to C
 *
 * Re-verified instruction by instruction against 0x00E1614E-0x00E1618E,
 * 0x00E16190-0x00E161F6 and 0x00E161F8-0x00E1633E (2026-09-22): the
 * argument frames (socket word at 0x8, record at 0xA, flags byte at 0xE),
 * the 0x18A0 view-pointer slot (slot 0 doubles as the spin lock), the
 * `tst.w / ble`, `cmpi.w #0xe0 / bls` socket range, the bit-13 / bit-15 /
 * `cmp.w (0x18,A0) / bls` admission tests, the unsigned queue_count <
 * max_queue test, every netbuf-header store, the n_hops `dbf` copy, the
 * tail linking and the four-page `cmp.l / ble` copy loop.
 */

#include "sock/sock_internal.h"

/*
 * SOCK_$PUT_INT_INT - Low-level packet queue insertion
 *
 * Performs the actual insertion of a packet into a socket's receive queue.
 * Copies packet metadata from the input buffer to the network buffer header.
 *
 * @param sock_view  Pointer to socket EC view
 * @param pkt_info   The packet record (0x00E16206 uses it directly)
 * @param flags      Domain boolean (see SOCK_$PUT_INT)
 * @param ec_param1  Event count parameter 1 (stored in netbuf)
 * @param ec_param2  Event count parameter 2 (stored in netbuf)
 *
 * @return 0 on success, 1 if queue full, 2 if socket not open
 */
int16_t SOCK_$PUT_INT_INT(sock_$sock_t *sock_view, sock_$pkt_info_t *pkt_info,
                          int8_t flags, uint16_t ec_param1, uint16_t ec_param2)
{
    ml_$spin_token_t token;
    int16_t result;
    uint8_t *netbuf;
    uint16_t data_len;
    int16_t i;

    /* Acquire spinlock */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /*
     * Validate socket state (0x00E1622A-0x00E16248):
     *   btst.l #0xd,D1        socket must be allocated
     *   tst.b D2b / bpl       if the flags byte is negative ...
     *   tst.w D1w / bpl       ... the socket must also be open (bit 15)
     *   move.w (0x2a,A2),D1w
     *   cmp.w (0x18,A0),D1w   data length must not exceed max_data_len
     *   bls                   (unsigned)
     */
    data_len = pkt_info->data_len;

    if ((sock_view->flags & SOCK_FLAG_ALLOCATED) == 0 ||
        ((flags < 0) && ((sock_view->flags & SOCK_FLAG_OPEN) == 0)) ||
        (data_len > sock_view->max_data_len)) {
        result = 2;  /* Socket not ready or data too large */
    } else if (sock_view->queue_count >= sock_view->max_queue) {
        /*
         * Queue is full: "move.b (0x15,A0),D5b / move.b (0x14,A0),D6b /
         * cmp.w D6w,D5w / bcs" at 0x00E1624C-0x00E1625A only proceeds while
         * queue_count < max_queue.
         */
        result = 1;
    } else {
        /* Increment queue count (0x00E16262 addq.b #1,(0x15,A0)) */
        sock_view->queue_count++;

        /*
         * Get the network buffer address by rounding the header VA down to
         * the 1KB page: "move.l (A2),D6 / andi.w #-0x400,D6w" at 0x00E16266
         * masks only the low word, which is the same as clearing bits 0..9.
         */
        netbuf = (uint8_t *)ARCH_VA_TO_PTR(pkt_info->hdr & 0xFFFFFC00u);

        /* Clear next pointer (end of queue), 0x00E1626E */
        *(uint32_t *)(netbuf + NETBUF_OFFSET_NEXT) = 0;

        /* Copy packet info to network buffer header (0x00E16272-0x00E1629E) */
        *(uint32_t *)(netbuf + NETBUF_OFFSET_SRC_ADDR) = pkt_info->src_addr;
        *(uint16_t *)(netbuf + NETBUF_OFFSET_SRC_PORT) = pkt_info->src_port;
        *(uint32_t *)(netbuf + NETBUF_OFFSET_DST_ADDR) = pkt_info->dst_addr;
        *(uint16_t *)(netbuf + NETBUF_OFFSET_DST_PORT) = pkt_info->flags;

        /*
         * "move.l (0x2a,A2),(0x3e8,A0)" at 0x00E1628A copies both the data
         * length and the header length in one longword.
         */
        *(uint16_t *)(netbuf + NETBUF_OFFSET_DATA_LEN)     = pkt_info->data_len;
        *(uint16_t *)(netbuf + NETBUF_OFFSET_DATA_LEN + 2) = pkt_info->hdr_len;

        /* Store EC parameters (0x00E16290, 0x00E16294) */
        *(uint16_t *)(netbuf + NETBUF_OFFSET_EC_PARAM1) = ec_param1;
        *(uint16_t *)(netbuf + NETBUF_OFFSET_EC_PARAM2) = ec_param2;

        /* Hop count and header pointer (0x00E16298, 0x00E1629E) */
        *(uint16_t *)(netbuf + NETBUF_OFFSET_HOP_COUNT) = pkt_info->n_hops;
        *(uint32_t *)(netbuf + NETBUF_OFFSET_HDR_PTR) = pkt_info->hdr;

        /* Copy the hop words (0x00E162A2-0x00E162BC, dbf = n_hops iterations) */
        {
            uint16_t hop_count = pkt_info->n_hops;
            uint16_t *hop_out = (uint16_t *)(netbuf + NETBUF_OFFSET_HOP_ARRAY);
            const uint16_t *hop_in = pkt_info->hops;

            for (i = (int16_t)hop_count - 1; i >= 0; i--) {
                *hop_out++ = *hop_in++;
            }
        }

        /* Link packet into queue (0x00E162C0-0x00E162E0) */
        if (sock_view->queue_tail == 0) {
            /* Queue was empty - packet is both head and tail */
            sock_view->queue_head = ARCH_PTR_TO_VA(netbuf);
            sock_view->queue_tail = ARCH_PTR_TO_VA(netbuf);
        } else {
            /* Append to existing queue */
            *(uint32_t *)((uint8_t *)ARCH_VA_TO_PTR(sock_view->queue_tail) +
                          NETBUF_OFFSET_NEXT) = ARCH_PTR_TO_VA(netbuf);
            sock_view->queue_tail = ARCH_PTR_TO_VA(netbuf);
        }

        /*
         * Copy the four data page addresses, zeroing the slots the payload
         * does not reach (0x00E162E4-0x00E1631E): the loop counter runs 1..4
         * and the comparison is "data_len <= (n-1) * 0x400".
         */
        for (i = 0; i < 4; i++) {
            if ((int32_t)data_len > (int32_t)i * 0x400) {
                *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_PTRS + i * 4) =
                    pkt_info->data_pages[i];
            } else {
                *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_PTRS + i * 4) = 0;
            }
        }

        result = 0;  /* Success */
    }

    /* Release spinlock */
    ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

    return result;
}

/*
 * SOCK_$PUT_INT - Mid-level packet queue insertion
 *
 * Validates socket number and calls SOCK_$PUT_INT_INT.
 * Returns the socket's event count pointer for use by caller.
 *
 * @param sock_num   Socket number (1..0xE0)
 * @param pkt_info   The packet record
 * @param flags      Domain boolean passed on to PUT_INT_INT
 * @param ec_param1  Event count parameter 1
 * @param ec_param2  Event count parameter 2
 * @param ec_ret     Output: pointer to socket's event count
 *
 * @return Negative on success, 0 on failure
 */
int8_t SOCK_$PUT_INT(uint16_t sock_num, sock_$pkt_info_t *pkt_info,
                     int8_t flags, uint16_t ec_param1, uint16_t ec_param2,
                     ec_$eventcount_t **ec_ret)
{
    sock_$sock_t *sock_view;
    int16_t put_result;

    /* Validate socket number */
    if (sock_num < 1 || sock_num > SOCK_MAX_NUMBER) {
        return 0;
    }

    /* Get pointer to socket's EC view */
    sock_view = SOCK_GET_VIEW_PTR(sock_num);

    /* Return EC pointer to caller */
    *ec_ret = &sock_view->ec;

    /*
     * 0x00E161CC: tst.b D2b / bpl.  When the boolean is true the socket's
     * queue_count is written into the packet's HEADER BUFFER at +0x0F -
     * "movea.l (A2),A0 / move.b (0x15,A3),(0xf,A0)" dereferences the record's
     * first longword, which is sock_$pkt_info_t.hdr.
     */
    if (flags < 0) {
        uint8_t *pkt_hdr = (uint8_t *)ARCH_VA_TO_PTR(pkt_info->hdr);
        pkt_hdr[0x0F] = sock_view->queue_count;
    }

    /* Perform the queue insertion */
    put_result = SOCK_$PUT_INT_INT(sock_view, pkt_info, flags,
                                   ec_param1, ec_param2);

    /* Return success (negative) if put_result is 0 */
    return (put_result == 0) ? -1 : 0;
}

/*
 * SOCK_$PUT - High-level packet queue insertion
 *
 * Queues a packet for delivery to a socket. If successful, advances
 * the socket's event count to wake any waiting processes.
 *
 * @param sock_num   Socket number
 * @param pkt_info   The packet record
 * @param flags      Domain boolean
 * @param ec_param1  Event count parameter 1
 * @param ec_param2  Event count parameter 2
 *
 * @return Negative (0xFF) if packet queued, 0 on error
 */
int8_t SOCK_$PUT(uint16_t sock_num, sock_$pkt_info_t *pkt_info, int8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2)
{
    ec_$eventcount_t *ec;
    int8_t result;

    /* Call mid-level PUT which returns EC pointer */
    result = SOCK_$PUT_INT(sock_num, pkt_info, flags, ec_param1, ec_param2,
                           &ec);

    if (result < 0) {
        /* Successfully queued - advance event count to wake waiters */
        EC_$ADVANCE(ec);
        result = -1;  /* 0xFF = success */
    } else {
        result = 0;
    }

    return result;
}
