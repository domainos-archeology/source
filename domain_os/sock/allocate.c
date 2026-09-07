/*
 * SOCK_$ALLOCATE - Allocate a socket from the free pool
 *
 * Allocates a socket with an automatically assigned socket number from
 * the dynamic range (32-223). The socket is taken from the free list.
 *
 * Original address: 0x00E15E62
 * Original source: Pascal, converted to C
 */

#include "sock/sock_internal.h"

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages, uint32_t max_queue)
{
    sock_$sock_t *sock_view;
    sock_$sock_t **free_list_head;
    ml_$spin_token_t token;
    int8_t result;

    /*
     * The original takes four separate words after the pointer: D2 = (0xC,A6),
     * D3 = (0xE,A6), D4 = (0x10,A6), D5 = (0x12,A6) (0x00E15E74-0x00E15E80).
     * The two longword parameters of this prototype pack them in pairs.
     */
    uint8_t  queue_limit = (uint8_t)((proto_bufpages >> 16) & 0xFF); /* D2b -> +0x14 */
    uint16_t hdr_pages   = (uint16_t)(proto_bufpages & 0xFFFF);      /* D3  -> +0x1A */
    uint16_t data_pages  = (uint16_t)((max_queue >> 16) & 0xFFFF);   /* D4  -> +0x1B */
    uint16_t max_len     = (uint16_t)(max_queue & 0xFFFF);           /* D5w -> +0x18 */

    /* Get pointer to free list head */
    free_list_head = SOCK_GET_FREE_LIST();

    /* Acquire spinlock to protect socket table */
    token = ML_$SPIN_LOCK(SOCK_GET_LOCK());

    /* Check if free list is empty (0x00E15E94 tst.l (0xc,A5)) */
    if (*free_list_head == NULL) {
        /* No free sockets available */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);
        *sock_ret = 0;
        result = 0;
    } else {
        /* Remove first socket from free list (0x00E15EAE) */
        sock_view = *free_list_head;
        *free_list_head = (sock_$sock_t *)sock_view->queue_head;

        /* Mark socket as allocated (0x00E15EBA bset.b #5,(0x16,A2)) */
        sock_view->flags |= SOCK_FLAG_ALLOCATED;

        /* Clear queue pointers (0x00E15EC0, 0x00E15EC4) */
        sock_view->queue_head = 0;
        sock_view->queue_tail = 0;

        /* Mark socket as open (0x00E15EC8 bset.b #7,(0x16,A2)) */
        sock_view->flags |= SOCK_FLAG_OPEN;

        /* Set socket parameters (0x00E15ECE-0x00E15EDA) */
        sock_view->data_pages   = (uint8_t)data_pages;   /* move.b D4b,(0x1b,A2) */
        sock_view->hdr_pages    = (uint8_t)hdr_pages;    /* move.b D3b,(0x1a,A2) */
        sock_view->max_data_len = max_len;               /* move.w D5w,(0x18,A2) */
        sock_view->max_queue    = queue_limit;           /* move.b D2b,(0x14,A2) */

        /* Release spinlock */
        ML_$SPIN_UNLOCK(SOCK_GET_LOCK(), token);

        /* Return the socket number (from flags bits 0-12), 0x00E15EEE */
        *sock_ret = SOCK_GET_NUMBER(sock_view->flags);

        /*
         * Allocate network buffer pages if either count is non-zero
         * (0x00E15EF8 "move.w D3w,D0w / or.w D4w,D0w / beq").  The original
         * pushes D4 then D3, so the longword NETBUF_$ADD_PAGES reads is
         * (hdr_pages << 16) | data_pages (0x00E15EFE-0x00E15F02).
         * TODO(source-ltga): NETBUF_$ADD_PAGES really takes two words
         * (0x00E0E936/0x00E0E93A); the packed longword is only the m68k
         * spelling of the same push sequence.
         */
        if ((hdr_pages | data_pages) != 0) {
            NETBUF_$ADD_PAGES(((uint32_t)hdr_pages << 16) | data_pages);
        }

        result = -1;  /* 0xFF = success */
    }

    return result;
}
