/*
 * AST_$PAGE_ZERO - Zero a physical page
 *
 * Zeros a physical page, acquiring the appropriate lock first.
 *
 * Original address: 0x00e00eec
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"

void AST_$PAGE_ZERO(uint32_t ppn)
{
    ML_$LOCK(PMAP_LOCK_ID);
    ZERO_PAGE(ppn);
    ML_$UNLOCK(PMAP_LOCK_ID);
}
