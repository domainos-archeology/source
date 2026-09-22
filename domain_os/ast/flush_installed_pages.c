/*
 * ast_$flush_installed_pages - Unmap and free the pages AST_$FREE_PAGES has
 *                              collected so far
 *
 * A Pascal procedure nested inside AST_$FREE_PAGES (0x00E0400C): it has no
 * parameters of its own and reaches the parent's frame through the static
 * link (`movea.l (A6),A2` at 0x00E03FC2).  The three uplevel cells it
 * touches are passed explicitly here:
 *
 *   ppn_array        parent (-0x100,A6): the collected page numbers
 *   installed_count  parent (-0x116,A6): how many, a word; zeroed on exit
 *   aste             parent (0x8,A6):    whose byte page count goes down
 *
 * It removes the pages from the MMU, gives them back to the pool, and
 * subtracts the count from the ASTE's page count.
 *
 * Original address: 0x00E03FBC (80 bytes).  Called from AST_$FREE_PAGES at
 * 0x00E04074, 0x00E040C0, 0x00E04100 and 0x00E0415C.
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

void ast_$flush_installed_pages(aste_t *aste, uint32_t *ppn_array,
                                uint16_t *installed_count)
{
    uint16_t count;         /* D0w */

    /* 0x00E03FC4..0x00E03FD4: MMU_$REMOVE_LIST(array, count) - the
     * `subq.l #0x2,SP` is an unused function result slot */
    MMU_$REMOVE_LIST(ppn_array, *installed_count);

    /* 0x00E03FD6..0x00E03FE4: three pushes - count, &array, PROC1_$CURRENT -
     * so MMAP_$FREE_PAGES(pid, array, count); the callee (0x00E0CE56) never
     * reads the pid word (source-8yhy). */
    MMAP_$FREE_PAGES(PROC1_$CURRENT, ppn_array, *installed_count);

    /* 0x00E03FEA..0x00E03FFC: sub.b D0b,D1b on the ASTE's byte page count */
    count = *installed_count;
    aste->page_count = (uint8_t)(aste->page_count - (uint8_t)count);

    /* 0x00E04000 */
    *installed_count = 0;
}
