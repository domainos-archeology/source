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

#include "sock/sock_internal.h"
#include "pkt/pkt.h"

void SOCK_$CLOSE(uint16_t sock_num)
{
    sock_$sock_t *sock_view;
    sock_$sock_t **free_list_head;
    uint16_t *user_limit;
    ml_$spin_token_t token;
    sock_$pkt_info_t pkt_info;
    int8_t get_result;

    /* Get pointer to socket's EC view */
    sock_view = SOCK_GET_VIEW_PTR(sock_num);

    /* Acquire spinlock */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* Clear the allocated flag */
    sock_view->flags &= ~SOCK_FLAG_ALLOCATED;

    /* Release spinlock */
    ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

    /* Drain any queued packets (0x00E15FBA tst on the byte at +0x15) */
    if (sock_view->queue_count != 0) {
        do {
            /* Get next packet from queue */
            get_result = SOCK_$GET(sock_num, &pkt_info);

            if (get_result < 0) {
                /* Return header buffer */
                NETBUF_$RTN_HDR(&pkt_info.hdr); /* pea (-0x40,A6) @0x00E15FC4 */

                /*
                 * If data pages are present, dump them.  The original tests
                 * the longword at record+0x30 (tst.l (-0x10,A6) @0x00E15FD0)
                 * and passes the WORD at record+0x2A as the length
                 * (move.w (-0x16,A6),-(SP) @0x00E15FD8).
                 */
                if (pkt_info.data_pages[0] != 0) {
                    PKT_$DUMP_DATA(pkt_info.data_pages, pkt_info.data_len);
                }
            }
        } while (get_result < 0);
    }

    /*
     * Return buffer pages if any were allocated.  0x00E15FF8-0x00E16006 ORs
     * the two bytes together; 0x00E1600A-0x00E16016 pushes +0x1B then +0x1A,
     * so +0x1A is the header count and +0x1B the data count.
     */
    if ((sock_view->hdr_pages | sock_view->data_pages) != 0) {
        NETBUF_$DEL_PAGES((int16_t)sock_view->hdr_pages,
                          (int16_t)sock_view->data_pages);
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

    /* Refuse further packets (0x00E16044 clr.b (0x14,A2)) */
    sock_view->max_queue = 0;

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
