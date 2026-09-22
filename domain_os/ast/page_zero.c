/*
 * AST_$PAGE_ZERO - Zero a physical page under the PMAP lock
 *
 * ZERO_PAGE (0x00E00EB0, mmu/) maps the frame at the wired AST_$ZERO_BUFF
 * address, clears its 256 longwords and unmaps it; this wrapper only
 * brackets that with ML_$LOCK / ML_$UNLOCK of lock 0x14.
 *
 * Original address: 0x00E00EEC (42 bytes).  `link.w A6,0x0`; no A5;
 * (0x8,A6) is the ppn longword, pushed straight through.
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"

void AST_$PAGE_ZERO(uint32_t ppn)
{
    /* 0x00E00EF0..0x00E00EFC */
    ML_$LOCK(PMAP_LOCK_ID);
    /* 0x00E00EFE..0x00E00F04 */
    ZERO_PAGE(ppn);
    /* 0x00E00F06..0x00E00F0C */
    ML_$UNLOCK(PMAP_LOCK_ID);
}
