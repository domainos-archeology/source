/*
 * os_$free_va_page - Free a page at a given virtual address
 *
 * Takes a virtual address, converts it to a physical page number
 * via VTOP_OR_CRASH, then removes the MMU mapping and frees
 * the physical page.
 *
 * This is used during OS initialization to free boot-time pages
 * that are no longer needed (e.g., init code pages).
 *
 * Parameters:
 *   vaddr - Virtual address of the page to free
 *
 * Original address: 0x00E6D240
 * Size: 20 bytes
 */

#include "os/os_internal.h"

/*
 * os_$free_ppn - Release a physical page number
 *
 * Removes the MMU mapping for the given physical page and
 * returns it to the free page pool.
 *
 * Original address: 0x00E6D222
 */
static void os_$free_ppn(uint32_t ppn)
{
    MMU_$REMOVE(ppn);
    MMAP_$FREE(ppn);
}

void os_$free_va_page(uint32_t vaddr)
{
    uint32_t ppn;

    /*
     * VTOP_OR_CRASH takes a pointer to the virtual address
     * (it dereferences the pointer to get the VA, then converts).
     * In the original assembly: pea (0x8,A6) pushes the address
     * of the stack parameter.
     */
    ppn = VTOP_OR_CRASH(vaddr);
    os_$free_ppn(ppn);
}
