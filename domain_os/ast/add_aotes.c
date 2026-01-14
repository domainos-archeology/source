/*
 * AST_$ADD_AOTES - Add AOTEs to the system
 *
 * Expands the AOTE pool by allocating memory and initializing
 * new AOTE entries. Each AOTE is 192 bytes (0xC0).
 *
 * Original address: 0x00e01014
 */

#include "ast.h"
#include "misc/misc.h"
#include "mmu/mmu.h"

uint16_t AST_$ADD_AOTES(uint16_t *count, status_$t *status)
{
    aote_t *aote_ptr;
    int16_t add_count;
    int16_t i, j;
    uint32_t ppn;
    uint32_t va;
    status_$t local_status;

    aote_ptr = AST_$AOTE_LIMIT;
    add_count = (int16_t)*count;
    local_status = status_$ok;

    /* Validate request */
    if ((int32_t)(add_count + AST_$SIZE_AOT) > AST_MAX_AOTE ||
        (int32_t)(add_count + AST_$SIZE_AOT) < AST_MIN_AOTE) {
        local_status = status_$ast_incompatible_request;
        goto done;
    }

    /* Ensure initial page is mapped */
    if (MMU_$VTOP((uint32_t)AST_$AOTE_LIMIT, &local_status) == 0 &&
        local_status != status_$ok) {
        WP_$CALLOC(&ppn, &local_status);
        if (local_status != status_$ok) {
            CRASH_SYSTEM(&local_status);
        }
        MMU_$INSTALL(ppn, (uint32_t)aote_ptr, 0x16);
    }

    ML_$LOCK(AST_LOCK_ID);

    /* Add each new AOTE */
    for (i = add_count - 1; i >= 0; i--) {
        aote_ptr = AST_$AOTE_LIMIT;
        va = (uint32_t)AST_$AOTE_LIMIT + 0xBF;  /* End of entry - 1 */
        AST_$AOTE_LIMIT = (aote_t*)((char*)AST_$AOTE_LIMIT + 0xC0);

        ML_$UNLOCK(AST_LOCK_ID);

        /* Ensure page for this AOTE is mapped */
        if (MMU_$VTOP(va, &local_status) == 0 && local_status != status_$ok) {
            WP_$CALLOC(&ppn, &local_status);
            if (local_status != status_$ok) {
                CRASH_SYSTEM(&local_status);
            }
            MMU_$INSTALL(ppn, va, 0x16);
        }

        /* Clear the AOTE */
        uint16_t *ptr = (uint16_t*)aote_ptr;
        for (j = 0x5F; j >= 0; j--) {
            *ptr++ = 0;
        }

        ML_$LOCK(AST_LOCK_ID);

        /* Add to free list (FUN_00e00f7c) */
        /* TODO: Call internal free function */
    }

    AST_$SIZE_AOT += add_count;
    ML_$UNLOCK(AST_LOCK_ID);

done:
    *status = local_status;
    return AST_$SIZE_AOT;
}
