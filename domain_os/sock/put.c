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
 */

#include "sock_internal.h"

/*
 * SOCK_$PUT_INT_INT - Low-level packet queue insertion
 *
 * Performs the actual insertion of a packet into a socket's receive queue.
 * Copies packet metadata from the input buffer to the network buffer header.
 *
 * @param sock_view  Pointer to socket EC view
 * @param pkt_ptr    Pointer to packet info pointer
 * @param flags      Flags (bit 7 = copy queue count to packet)
 * @param ec_param1  Event count parameter 1 (stored in netbuf)
 * @param ec_param2  Event count parameter 2 (stored in netbuf)
 *
 * @return 0 on success, 1 if queue full, 2 if socket not open
 */
int16_t SOCK_$PUT_INT_INT(sock_ec_view_t *sock_view, void **pkt_ptr,
                          int8_t flags, uint16_t ec_param1, uint16_t ec_param2)
{
    ml_$spin_token_t token;
    int16_t result;
    uint8_t *netbuf;
    uint32_t *pkt_info = (uint32_t *)*pkt_ptr;
    uint16_t data_len;
    int16_t i;

    /* Acquire spinlock */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /*
     * Validate socket state:
     * - Socket must be allocated (bit 13 set)
     * - If flags bit 7 is set, socket must also be open (bit 15 set)
     * - Data length must not exceed max_queue
     */
    data_len = *(uint16_t *)((uint8_t *)pkt_info + 0x2A);

    if ((sock_view->flags & SOCK_FLAG_ALLOCATED) == 0 ||
        ((flags < 0) && ((sock_view->flags & SOCK_FLAG_OPEN) == 0)) ||
        (data_len > sock_view->max_queue)) {
        result = 2;  /* Socket not ready or data too large */
    } else if (sock_view->queue_count >= sock_view->protocol) {
        /* Queue is full (protocol field doubles as max queue depth here) */
        result = 1;
    } else {
        /* Increment queue count */
        sock_view->queue_count++;

        /* Get network buffer address (aligned to 1KB boundary) */
        netbuf = (uint8_t *)((uint32_t)pkt_info[0] & 0xFFFFFC00);

        /* Clear next pointer (end of queue) */
        *(uint32_t *)(netbuf + NETBUF_OFFSET_NEXT) = 0;

        /* Copy packet info to network buffer header */
        *(uint32_t *)(netbuf + NETBUF_OFFSET_SRC_ADDR) = pkt_info[1];
        *(uint16_t *)(netbuf + NETBUF_OFFSET_SRC_PORT) = *(uint16_t *)&pkt_info[2];
        *(uint32_t *)(netbuf + NETBUF_OFFSET_DST_ADDR) = pkt_info[3];
        *(uint16_t *)(netbuf + NETBUF_OFFSET_DST_PORT) = *(uint16_t *)&pkt_info[4];
        *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_LEN) = *(uint32_t *)((uint8_t *)pkt_info + 0x2A);

        /* Store EC parameters */
        *(uint16_t *)(netbuf + NETBUF_OFFSET_EC_PARAM1) = ec_param1;
        *(uint16_t *)(netbuf + NETBUF_OFFSET_EC_PARAM2) = ec_param2;

        /* Copy hop count and hop array */
        *(uint16_t *)(netbuf + NETBUF_OFFSET_HOP_COUNT) = *(uint16_t *)((uint8_t *)pkt_info + 0x12);
        *(uint32_t *)(netbuf + NETBUF_OFFSET_HDR_PTR) = pkt_info[0];

        /* Copy hop array if present */
        uint16_t hop_count = *(uint16_t *)((uint8_t *)pkt_info + 0x12);
        if (hop_count > 0) {
            uint16_t *hop_out = (uint16_t *)(netbuf + NETBUF_OFFSET_HOP_ARRAY);
            uint16_t *hop_in = (uint16_t *)((uint8_t *)pkt_info + 0x14);

            for (i = hop_count - 1; i >= 0; i--) {
                *hop_out++ = *hop_in++;
            }
        }

        /* Link packet into queue */
        if (sock_view->queue_tail == 0) {
            /* Queue was empty - packet is both head and tail */
            sock_view->queue_head = (uint32_t)netbuf;
            sock_view->queue_tail = (uint32_t)netbuf;
        } else {
            /* Append to existing queue */
            *(uint32_t *)(sock_view->queue_tail + NETBUF_OFFSET_NEXT) = (uint32_t)netbuf;
            sock_view->queue_tail = (uint32_t)netbuf;
        }

        /*
         * Copy data page pointers.
         * Up to 4 pages, each page is 1KB. Only copy pointers for
         * pages that contain data based on data_len.
         */
        for (i = 0; i < 4; i++) {
            if (((i) * 0x400) < data_len) {
                *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_LEN + 4 + i * 4) =
                    pkt_info[0x0C + i];
            } else {
                *(uint32_t *)(netbuf + NETBUF_OFFSET_DATA_LEN + 4 + i * 4) = 0;
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
 * @param sock_num   Socket number (1-223)
 * @param pkt_ptr    Pointer to packet info pointer
 * @param flags      Flags passed to PUT_INT_INT
 * @param ec_param1  Event count parameter 1
 * @param ec_param2  Event count parameter 2
 * @param ec_ret     Output: pointer to socket's event count
 *
 * @return Negative on success, 0 on failure
 */
int8_t SOCK_$PUT_INT(uint16_t sock_num, void **pkt_ptr, uint8_t flags,
                     uint16_t ec_param1, uint16_t ec_param2,
                     ec_$eventcount_t **ec_ret)
{
    sock_ec_view_t *sock_view;
    int16_t put_result;

    /* Validate socket number */
    if (sock_num < 1 || sock_num > SOCK_MAX_NUMBER) {
        return 0;
    }

    /* Get pointer to socket's EC view */
    sock_view = SOCK_GET_VIEW_PTR(sock_num);

    /* Return EC pointer to caller */
    *ec_ret = &sock_view->ec;

    /* If flags bit 7 is set, copy queue count to packet header byte 0x0F */
    if ((int8_t)flags < 0) {
        uint8_t *pkt_hdr = (uint8_t *)*pkt_ptr;
        pkt_hdr[0x0F] = sock_view->queue_count;
    }

    /* Perform the queue insertion */
    put_result = SOCK_$PUT_INT_INT(sock_view, pkt_ptr, (int8_t)flags,
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
 * @param pkt_ptr    Pointer to packet info pointer
 * @param flags      Flags
 * @param ec_param1  Event count parameter 1
 * @param ec_param2  Event count parameter 2
 *
 * @return Negative (0xFF) if packet queued, 0 on error
 */
int8_t SOCK_$PUT(uint16_t sock_num, void **pkt_ptr, uint8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2)
{
    ec_$eventcount_t *ec;
    int8_t result;

    /* Call mid-level PUT which returns EC pointer */
    result = SOCK_$PUT_INT(sock_num, pkt_ptr, flags, ec_param1, ec_param2, &ec);

    if (result < 0) {
        /* Successfully queued - advance event count to wake waiters */
        EC_$ADVANCE(ec);
        result = -1;  /* 0xFF = success */
    } else {
        result = 0;
    }

    return result;
}
