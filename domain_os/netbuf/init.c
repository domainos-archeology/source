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
 *   piVar3 = &DAT_00e245a8;
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

    /* Terminate free list - last valid slot points to -1 */
    NETBUF_$VA_TOP = (int32_t)-1;

    /* Set data buffer limit to half of pageable pages */
    NETBUF_$DAT_LIM = MMAP_$PAGEABLE_PAGES_LOWER_LIMIT >> 1;

    /* Allocate initial buffers: 0x27 (39) header buffers, 0x0a (10) data buffers
     * Packed as: (hdr_count << 16) | dat_count = 0x27000a
     */
    NETBUF_$ADD_PAGES(0x27000a);
}
