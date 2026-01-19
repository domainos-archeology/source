/*
 * SOCK_$INIT - Initialize socket subsystem
 *
 * Initializes all socket descriptors, event counts, and the free list.
 * Socket numbers 0-31 are reserved for well-known services.
 * Socket numbers 32-223 are added to the free list for dynamic allocation.
 *
 * Original address: 0x00E2FDF0
 * Original source: Pascal, converted to C
 */

#include "sock_internal.h"

void SOCK_$INIT(void)
{
    int16_t counter;
    uint16_t sock_num;
    uint8_t *sock_desc_ptr;
    uint8_t *ptr_array_base;
    sock_ec_view_t **free_list_head;

    /*
     * Initialize loop variables:
     * - counter: decrements from 0xDF (223) to -1 (224 iterations for sockets 1-224)
     * - sock_num: socket number, starts at 1
     * - sock_desc_ptr: pointer to current socket descriptor
     * - ptr_array_base: base for indexing into pointer array
     */
    counter = SOCK_MAX_SOCKETS - 1;  /* 0xDF = 223 */
    sock_num = 1;
    sock_desc_ptr = sock_table_base + SOCK_TABLE_FIRST_DESC;  /* First descriptor at +0x1C */
    ptr_array_base = sock_table_base + 4;  /* Offset for pointer array indexing */

    /* Get pointer to free list head */
    free_list_head = SOCK_GET_FREE_LIST();

    do {
        sock_ec_view_t *ec_view = (sock_ec_view_t *)(sock_desc_ptr + 4);

        /*
         * Store EC pointer in the pointer array.
         * Array is at base + 0x18A0, indexed by (ptr_array_base - base - 4) / 4 + 1
         * which equals sock_num. So slot sock_num stores pointer to sock_num's EC.
         */
        *(sock_ec_view_t **)(ptr_array_base + SOCK_TABLE_LOCK) = ec_view;

        /* Initialize the event count */
        EC_$INIT(&ec_view->ec);

        /*
         * Set the socket number in the flags field.
         * Bits 0-12 hold the socket number, bits 13-15 are cleared (socket not allocated).
         */
        ec_view->flags = (ec_view->flags & 0xE000) | (sock_num & SOCK_FLAG_NUMBER_MASK);

        /*
         * Add sockets >= 32 to the free list.
         * Sockets 0-31 are reserved for well-known services and not in the free list.
         */
        if (sock_num > SOCK_RESERVED_MAX) {
            /* Link this socket into the free list */
            ec_view->queue_tail = (uint32_t)*free_list_head;
            *free_list_head = ec_view;
        }

        /* Advance to next socket */
        sock_num++;
        ptr_array_base += 4;
        sock_desc_ptr += SOCK_DESC_SIZE;
        counter--;
    } while (counter != -1);
}
