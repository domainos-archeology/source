/*
 * SOCK_$INIT - Initialize socket subsystem
 *
 * Initializes all socket descriptors, event counts, and the free list.
 * Socket numbers 0-31 are reserved for well-known services.
 * Socket numbers 32-224 are added to the free list for dynamic allocation.
 *
 * Original address: 0x00E2FDF0
 * Original source: Pascal, converted to C
 *
 * Module data block conversion (SOCK_$DATA): Claude Opus 5.5 (source-gy7x).
 */

#include "sock/sock_internal.h"

void SOCK_$INIT(void)
{
    int16_t counter;
    int16_t sock_num;           /* the frame word at (-0x2,A6) */
    sock_$sock_t *sock;

    /*
     * 0x00E2FDF8-0x00E2FE14: D2 = 0xDF (dbf, 224 passes), the socket number
     * starts at 1, A4 walks the descriptors from the block base + 0x1C and
     * A3 the pointer table from the block base + 4, so pass n touches
     * SOCK_$DATA.socket[n] (A4 + 4) and SOCK_$DATA.socket_ptr[n]
     * (A3 + 0x18A0).
     */
    counter = SOCK_MAX_SOCKETS - 1;
    sock_num = 1;

    do {
        sock = &SOCK_$DATA.socket[sock_num];

        /* 0x00E2FE16-0x00E2FE1A: lea (0x4,A2),A0 / move.l A0,(0x18a0,A3) */
        SOCK_$DATA.socket_ptr[sock_num] = sock;

        /* 0x00E2FE1E: EC_$INIT(&descriptor) */
        EC_$INIT(&sock->ec);

        /*
         * 0x00E2FE2A-0x00E2FE38: keep bits 13..15 of the flags word and put
         * the socket number in bits 0..12.
         */
        sock->flags &= 0xE000;
        sock->flags |= (uint16_t)(sock_num & SOCK_FLAG_NUMBER_MASK);

        /*
         * 0x00E2FE3C-0x00E2FE50: "cmpi.w #0x20,(-0x2,A6) / blt" - sockets
         * 0x20 and up are pushed on the free list through queue_head
         * ("move.l (0xc,A0),(0x10,A2)", A2 + 0x10 = record + 0x0C), and the
         * head at block + 0x0C takes the record's address.
         */
        if (sock_num >= SOCK_DYNAMIC_MIN) {
            sock->queue_head = SOCK_$DATA.list.free_head;
            SOCK_$DATA.list.free_head = ARCH_PTR_TO_VA(sock);
        }

        /* 0x00E2FE54-0x00E2FE5E */
        sock_num++;
        counter--;
    } while (counter != -1);
}
