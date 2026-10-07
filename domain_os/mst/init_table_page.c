/*
 * mst_$init_table_page - Allocate, map and zero one MST page-table page
 *
 * Called only from MST_$ALLOC_TABLE_PAGE (0xE43FBE) once a free page index
 * has been claimed; the argument is the page's virtual address, already
 * computed as MSTE_PAGES (0xEF6400 in the image) + page_index * 0x400 -
 * 0x400.
 *
 * Original address: 0x00E42CEC
 * Original size: 78 bytes
 *
 * Assembly:
 *   00e42cec    link.w A6,-0x18
 *   00e42cf0    movem.l { A2 D2 },-(SP)
 *   00e42cf4    move.l (0x8,A6),D0
 *   00e42cf8    andi.w #-0x400,D0w      ; WORD mask: only the low half is
 *   00e42cfc    movea.l D0,A2           ;  masked, so this is va & 0xFFFFFC00
 *   00e42cfe    pea (-0x8,A6)           ; &status
 *   00e42d02    pea (-0x10,A6)          ; &ppn
 *   00e42d06    jsr WP_$CALLOC
 *   00e42d0c    addq.w #0x8,SP
 *   00e42d0e    move.l (-0x10,A6),D2    ; ppn
 *   00e42d12    pea (0x16).w            ; flags = 0x16 (pushed as a long)
 *   00e42d16    pea (A2)                ; page-aligned VA
 *   00e42d18    move.l D2,-(SP)         ; ppn
 *   00e42d1a    jsr MMU_$INSTALL
 *   00e42d20    lea (0xc,SP),SP
 *   00e42d24    move.w #0xff,D0w        ; 0xFF+1 = 256 iterations
 *   00e42d28    clr.l (A2)+
 *   00e42d2a    dbf D0w,0x00e42d28      ; 256 * 4 = 0x400 bytes zeroed
 *   00e42d2e    move.l D2,D0            ; result = the allocated PPN
 *   00e42d30    movem.l (-0x20,A6),{ D2 A2 }
 *   00e42d36    unlk A6
 *   00e42d38    rts
 *
 * Notes on fidelity:
 *   - WP_$CALLOC's status output at (-0x8,A6) is written but NEVER tested;
 *     the local below reproduces that (the page is installed and zeroed even
 *     if the allocation failed).
 *   - The caller pushes the argument with "pea" and never adjusts SP; the
 *     stack is recovered by its own "unlk".  Neither routine pops the
 *     argument, which has no effect on the C translation.
 *   - The result in D0 is the PPN.  MST_$ALLOC_TABLE_PAGE ignores it.
 */

#include "mst/mst_internal.h"
#include "mmu/mmu.h"
#include "wp/wp.h"

/*
 * MMU protection/ASID word installed for an MST page-table page.
 * 0xE42D12 pushes 0x16: ASID 0 (global) and protection 0x16.
 */
#define MST_TABLE_PAGE_MMU_FLAGS  0x16    /* prot; asid 0 (pea (0x16).w) */

/* clr.l (A2)+ / dbf #0xFF: 256 longwords = one 0x400-byte MST page. */
#define MST_TABLE_PAGE_LONGS      256

/*
 * The virtual address is pointer-width.  On the m68k target uintptr_t is
 * exactly the 32-bit uint32_t the original uses; spelling it uintptr_t keeps
 * the routine testable on a 64-bit host without changing target codegen.
 */
#if defined(ARCH_M68K)
_Static_assert(sizeof(uintptr_t) == 4, "m68k virtual addresses are 32 bits");
#endif

uint32_t mst_$init_table_page(uintptr_t page_addr)
{
    status_$t status;
    uint32_t ppn;
    uint32_t *page;
    int16_t i;

    /*
     * 0xE42CF8 "andi.w #-0x400,D0w" masks the LOW word with 0xFC00, leaving
     * the high half untouched: bits 10-31 are kept and bits 0-9 cleared,
     * i.e. round down to the 0x400-byte page.
     */
    page = (uint32_t *)(page_addr & ~(uintptr_t)0x3FF);

    WP_$CALLOC(&ppn, &status);
    /* status is deliberately not examined - see the note above. */

    MMU_$INSTALL(ppn, (uint32_t)(uintptr_t)page, 0, MST_TABLE_PAGE_MMU_FLAGS);

    for (i = MST_TABLE_PAGE_LONGS - 1; i >= 0; i--) {
        *page++ = 0;
    }

    return ppn;
}
