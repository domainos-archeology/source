/*
 * NETBUF_$INIT - Initialize network buffer subsystem
 *
 * Initializes the VA slot free list and allocates initial buffers.
 *
 * The VA slot array is initialized as a free list where each entry
 * contains the index of the next free slot (1, 2, 3, ..., 191, -1).
 *
 * Original address: 0x00E2F630
 *
 * Ghidra decompilation:
 *   DAT_00e248d8 = 0xd64c00;
 *   sVar1 = 0xbf;  // 191
 *   iVar2 = 0;
 *   piVar3 = &NETBUF_$DATA;    (was DAT_00e245a8)
 *   do {
 *     iVar2 = iVar2 + 1;
 *     *piVar3 = iVar2;
 *     sVar1 = sVar1 + -1;
 *     piVar3 = piVar3 + 1;
 *   } while (sVar1 != -1);
 *   DAT_00e248a4 = 0xffffffff;
 *   NETBUF__DAT_LIM = MMAP__PAGEABLE_PAGES_LOWER_LIMIT >> 1;
 *   NETBUF__ADD_PAGES(0x27000a,(short)((uint)unaff_D2 >> 0x10));
 */

#include "netbuf/netbuf_internal.h"

void NETBUF_$INIT(void)
{
    int i;

    /* Set VA base address */
    NETBUF_$VA_BASE_ADDR = NETBUF_$VA_BASE;

    /* Initialize VA slot free list
     * Each slot points to the next slot index (1, 2, 3, ..., 191)
     * After loop: slot[0]=1, slot[1]=2, ..., slot[190]=191
     */
    for (i = 0; i < NETBUF_VA_SLOTS; i++) {
        NETBUF_$VA_SLOTS[i] = i + 1;
    }

    /*
     * 0x00E2F658..0x00E2F662  moveq #-1,D2 / move.l D2,(0x2fc,A1): the LAST
     * slot (va_slots[191], offset 0x2FC) terminates the free list; the head
     * va_top (0x32C) is not written and keeps the data block's 0, so the
     * list is 0 -> 1 -> ... -> 191 -> -1.  (A previous translation wrote the
     * -1 into va_top itself, which made the very first NETBUF_$GETVA report
     * status_$network_out_of_blocks: boot run 6, source-uaea.)
     */
    NETBUF_$VA_SLOTS[NETBUF_VA_SLOTS - 1] = (uint32_t)-1;

    /* Set data buffer limit to half of pageable pages */
    NETBUF_$DAT_LIM = MMAP_$PAGEABLE_PAGES >> 1;

    /*
     * Allocate the initial buffers: 0x27 (39) header pages and 0x0A (10) data
     * pages.  The original pushes the two words adjacently, which Ghidra
     * renders as the single longword 0x27000A.
     */
    NETBUF_$ADD_PAGES(0x27, 0x0A);
}
