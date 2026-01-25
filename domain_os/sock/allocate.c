/*
 * SOCK_$ALLOCATE - Allocate a socket from the free pool
 *
 * Allocates a socket with an automatically assigned socket number from
 * the dynamic range (32-223). The socket is taken from the free list.
 *
 * Original address: 0x00E15E62
 * Original source: Pascal, converted to C
 */

#include "sock_internal.h"

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages, uint32_t max_queue)
{
    sock_ec_view_t *sock_view;
    sock_ec_view_t **free_list_head;
    ml_$spin_token_t token;
    int8_t result;

    /* Unpack the combined parameter */
    uint8_t protocol = (uint8_t)((proto_bufpages >> 16) & 0xFF);
    uint16_t buffer_pages = (uint16_t)(proto_bufpages & 0xFFFF);

    /* Get pointer to free list head */
    free_list_head = SOCK_GET_FREE_LIST();

    /* Acquire spinlock to protect socket table */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* Check if free list is empty */
    if (*free_list_head == NULL) {
        /* No free sockets available */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);
        *sock_ret = 0;
        result = 0;
    } else {
        /* Remove first socket from free list */
        sock_view = *free_list_head;
        *free_list_head = (sock_ec_view_t *)sock_view->queue_head;

        /* Mark socket as allocated */
        sock_view->flags |= SOCK_FLAG_ALLOCATED;

        /* Clear queue pointers */
        sock_view->queue_head = 0;
        sock_view->queue_tail = 0;

        /* Mark socket as open */
        sock_view->flags |= SOCK_FLAG_OPEN;

        /* Set socket parameters */
        sock_view->buffer_pages_lo = (uint8_t)((buffer_pages >> 8) & 0xFF);
        sock_view->buffer_pages_hi = (uint8_t)(buffer_pages & 0xFF);
        sock_view->max_queue = (uint16_t)max_queue;

        /* Set protocol */
        sock_view->protocol = protocol;

        /* Release spinlock */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

        /* Return the socket number (from flags bits 0-12) */
        *sock_ret = SOCK_GET_NUMBER(sock_view->flags);

        /* Allocate network buffer pages if requested */
        if (buffer_pages != 0) {
            NETBUF_$ADD_PAGES((uint32_t)buffer_pages);
        }

        result = -1;  /* 0xFF = success */
    }

    return result;
}
