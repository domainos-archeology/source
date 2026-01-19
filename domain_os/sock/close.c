/*
 * SOCK_$CLOSE - Close a socket
 *
 * Closes an open socket, draining any queued packets and returning
 * allocated buffers to the pool. For dynamic sockets (>= 32), returns
 * the socket to the free list for reuse.
 *
 * For user-mode sockets, increments the user socket limit counter.
 *
 * Original address: 0x00E15F72
 * Original source: Pascal, converted to C
 */

#include "sock_internal.h"
#include "pkt/pkt.h"

void SOCK_$CLOSE(uint16_t sock_num)
{
    sock_ec_view_t *sock_view;
    sock_ec_view_t **free_list_head;
    uint16_t *user_limit;
    ml_$spin_token_t token;
    sock_pkt_info_t pkt_info;
    int8_t get_result;

    /* Get pointer to socket's EC view */
    sock_view = SOCK_GET_VIEW_PTR(sock_num);

    /* Acquire spinlock */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* Clear the allocated flag */
    sock_view->flags &= ~SOCK_FLAG_ALLOCATED;

    /* Release spinlock */
    ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

    /* Drain any queued packets */
    if (sock_view->queue_count != 0) {
        do {
            /* Get next packet from queue */
            get_result = SOCK_$GET(sock_num, &pkt_info);

            if (get_result < 0) {
                /* Return header buffer */
                NETBUF_$RTN_HDR(&pkt_info.hdr_ptr);

                /* If data pointers present, dump the data */
                if (pkt_info.data_ptrs[0] != 0) {
                    PKT_$DUMP_DATA(pkt_info.data_ptrs, pkt_info.data_len);
                }
            }
        } while (get_result < 0);
    }

    /* Return buffer pages if allocated */
    if (sock_view->buffer_pages_hi != 0 || sock_view->buffer_pages_lo != 0) {
        NETBUF_$DEL_PAGES((int16_t)sock_view->buffer_pages_hi,
                         (int16_t)sock_view->buffer_pages_lo);
    }

    /* Acquire spinlock again for final cleanup */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* If this was a user-mode socket, restore the user limit */
    if ((sock_view->flags & SOCK_FLAG_USER_MODE) != 0) {
        user_limit = SOCK_GET_USER_LIMIT();
        (*user_limit)++;

        /* Clear user-mode flag */
        sock_view->flags &= ~SOCK_FLAG_USER_MODE;
    }

    /* Clear protocol */
    sock_view->protocol = 0;

    /* For dynamic sockets (>= 32), return to free list */
    if (sock_num >= SOCK_DYNAMIC_MIN) {
        free_list_head = SOCK_GET_FREE_LIST();

        /* Link into free list */
        sock_view->queue_head = (uint32_t)*free_list_head;
        *free_list_head = sock_view;
    }

    /* Release spinlock */
    ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);
}
