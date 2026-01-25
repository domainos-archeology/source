/*
 * SOCK_$OPEN - Open a socket with a specific socket number
 *
 * Opens a socket for a well-known service (socket numbers 0-31) or
 * claims a specific socket number in the dynamic range (32-223).
 *
 * For dynamic sockets (>= 32), removes the socket from the free list.
 * Allocates network buffer pages if requested.
 *
 * Original address: 0x00E15D8C
 * Original source: Pascal, converted to C
 */

#include "sock_internal.h"

int8_t SOCK_$OPEN(uint16_t sock_num, uint32_t proto_bufpages, uint32_t max_queue)
{
    sock_ec_view_t *sock_view;
    ml_$spin_token_t token;
    int8_t result;

    /* Unpack the combined parameter */
    uint8_t protocol = (uint8_t)((proto_bufpages >> 16) & 0xFF);
    uint16_t buffer_pages = (uint16_t)(proto_bufpages & 0xFFFF);

    /* Get pointer to socket's EC view from pointer table */
    sock_view = SOCK_GET_VIEW_PTR(sock_num);

    /* Acquire spinlock to protect socket table */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* Check if socket is already allocated */
    if ((sock_view->flags & SOCK_FLAG_ALLOCATED) != 0) {
        /* Socket already in use - fail */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);
        result = 0;
    } else {
        /* Mark socket as allocated */
        sock_view->flags |= SOCK_FLAG_ALLOCATED;

        /* Clear protocol temporarily */
        sock_view->protocol = 0;

        /* If this is a dynamic socket (>= 32), remove from free list */
        if (SOCK_GET_NUMBER(sock_view->flags) > SOCK_RESERVED_MAX) {
            sock_ec_view_t **prev_ptr = SOCK_GET_FREE_LIST();

            /* Find this socket in the free list */
            while (*prev_ptr != sock_view) {
                prev_ptr = (sock_ec_view_t **)&((*prev_ptr)->queue_head);
            }
            /* Remove from list by linking previous to next */
            *prev_ptr = (sock_ec_view_t *)sock_view->queue_head;
        }

        /* Initialize queue pointers */
        sock_view->queue_head = 0;
        sock_view->queue_tail = 0;

        /* Set socket parameters */
        sock_view->buffer_pages_lo = (uint8_t)(buffer_pages & 0xFF);
        sock_view->buffer_pages_hi = (uint8_t)((buffer_pages >> 8) & 0xFF);
        sock_view->max_queue = (uint16_t)max_queue;

        /* Mark socket as open */
        sock_view->flags |= SOCK_FLAG_OPEN;

        /* Set protocol */
        sock_view->protocol = protocol;

        /* Release spinlock */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

        /* Allocate network buffer pages if requested */
        if (buffer_pages != 0) {
            NETBUF_$ADD_PAGES((uint32_t)buffer_pages);
        }

        result = -1;  /* 0xFF = success */
    }

    return result;
}
