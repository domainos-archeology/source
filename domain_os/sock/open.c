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

#include "sock/sock_internal.h"

int8_t SOCK_$OPEN(uint16_t sock_num, uint32_t proto_bufpages, uint32_t max_queue)
{
    sock_$sock_t *sock_view;
    ml_$spin_token_t token;
    int8_t result;

    /*
     * As in SOCK_$ALLOCATE the original reads four separate words:
     * D2 = (0xA,A6), D3 = (0xC,A6), D4 = (0xE,A6), D5 = (0x10,A6)
     * (0x00E15D9E-0x00E15DAC).
     */
    uint8_t  queue_limit = (uint8_t)((proto_bufpages >> 16) & 0xFF); /* D2b -> +0x14 */
    uint16_t hdr_pages   = (uint16_t)(proto_bufpages & 0xFFFF);      /* D3  -> +0x1A */
    uint16_t data_pages  = (uint16_t)((max_queue >> 16) & 0xFFFF);   /* D4  -> +0x1B */
    uint16_t max_len     = (uint16_t)(max_queue & 0xFFFF);           /* D5w -> +0x18 */

    /* Get pointer to socket's descriptor from the pointer table */
    sock_view = SOCK_GET_VIEW_PTR(sock_num);

    /* Acquire spinlock to protect socket table */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* Check if socket is already allocated (0x00E15DD0 btst.l #0xd,D1) */
    if ((sock_view->flags & SOCK_FLAG_ALLOCATED) != 0) {
        /* Socket already in use - fail */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);
        result = 0;
    } else {
        /* Mark socket as allocated (0x00E15DE8 bset.b #5,(0x16,A2)) */
        sock_view->flags |= SOCK_FLAG_ALLOCATED;

        /* Refuse further packets while the socket is being set up
         * (0x00E15DEE clr.b (0x14,A2)) */
        sock_view->max_queue = 0;

        /*
         * If this is a dynamic socket (>= 32), unlink it from the free list.
         * The original walks with A0 starting at the table base, comparing
         * (0xc,A0) - i.e. the free-list head - against A2 (0x00E15E00-0x00E15E0E).
         */
        if (SOCK_GET_NUMBER(sock_view->flags) > SOCK_RESERVED_MAX) {
            sock_$sock_t **prev_ptr = SOCK_GET_FREE_LIST();

            while (*prev_ptr != sock_view) {
                prev_ptr = (sock_$sock_t **)&((*prev_ptr)->queue_head);
            }
            *prev_ptr = (sock_$sock_t *)sock_view->queue_head;
        }

        /* Initialize queue pointers (0x00E15E14, 0x00E15E18) */
        sock_view->queue_head = 0;
        sock_view->queue_tail = 0;

        /* Set socket parameters (0x00E15E1C-0x00E15E24) */
        sock_view->data_pages   = (uint8_t)data_pages;   /* move.b D4b,(0x1b,A2) */
        sock_view->hdr_pages    = (uint8_t)hdr_pages;    /* move.b D3b,(0x1a,A2) */
        sock_view->max_data_len = max_len;               /* move.w D5w,(0x18,A2) */

        /* Mark socket as open (0x00E15E28 bset.b #7,(0x16,A2)) */
        sock_view->flags |= SOCK_FLAG_OPEN;

        /* Admit packets (0x00E15E2E move.b D2b,(0x14,A2)) */
        sock_view->max_queue = queue_limit;

        /* Release spinlock */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

        /*
         * Allocate network buffer pages if either count is non-zero
         * (0x00E15E44 "tst.w D3w / bne / tst.w D4w / beq").  The push order
         * at 0x00E15E4C makes the longword (hdr_pages << 16) | data_pages.
         * TODO(source-ltga): NETBUF_$ADD_PAGES really takes two words.
         */
        if (hdr_pages != 0 || data_pages != 0) {
            NETBUF_$ADD_PAGES(((uint32_t)hdr_pages << 16) | data_pages);
        }

        result = -1;  /* 0xFF = success */
    }

    return result;
}
