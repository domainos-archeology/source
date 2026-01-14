/*
 * AST_$ADD_ASTES - Add ASTEs to the system
 *
 * Expands the ASTE pool by allocating memory and initializing
 * new ASTE entries. Each ASTE is 20 bytes (0x14), and each also
 * requires a 128-byte (0x80) segment map entry.
 *
 * Original address: 0x00e0118c
 */

#include "ast.h"
#include "misc/misc.h"
#include "mmu/mmu.h"

uint16_t AST_$ADD_ASTES(uint16_t *count, status_$t *status)
{
    aste_t *aste_ptr;
    segmap_entry_t *segmap_ptr;
    int16_t add_count;
    int16_t i, j;
    uint16_t seg_index;
    uint32_t ppn;
    uint32_t va;
    status_$t local_status;

    aste_ptr = AST_$ASTE_LIMIT;
    add_count = (int16_t)*count;
    local_status = status_$ok;

    /* Validate request */
    if ((int32_t)(add_count + AST_$SIZE_AST) > AST_MAX_ASTE ||
        (int32_t)(add_count + AST_$SIZE_AST) < AST_MIN_ASTE) {
        local_status = status_$ast_incompatible_request;
        goto done;
    }

    /* Ensure initial page is mapped */
    if (MMU_$VTOP((uint32_t)AST_$ASTE_LIMIT, &local_status) == 0 &&
        local_status != status_$ok) {
        WP_$CALLOC(&ppn, &local_status);
        if (local_status != status_$ok) {
            CRASH_SYSTEM(&local_status);
        }
        MMU_$INSTALL(ppn, (uint32_t)aste_ptr, 0x16);
    }

    ML_$LOCK(AST_LOCK_ID);

    /* Calculate starting segment index */
    seg_index = AST_$SIZE_AST + 1;

    /* Add each new ASTE */
    for (i = (add_count + AST_$SIZE_AST) - (AST_$SIZE_AST + 1); i >= 0; i--) {
        aste_ptr = AST_$ASTE_LIMIT;
        va = (uint32_t)AST_$ASTE_LIMIT + 0x13;  /* End of entry - 1 */
        AST_$ASTE_LIMIT = (aste_t*)((char*)AST_$ASTE_LIMIT + sizeof(aste_t));

        /* Calculate segment map address */
        segmap_ptr = (segmap_entry_t*)((char*)SEGMAP_BASE + ((uint32_t)seg_index << 7));

        ML_$UNLOCK(AST_LOCK_ID);

        /* Ensure page for this ASTE is mapped */
        if (MMU_$VTOP(va, &local_status) == 0 && local_status != status_$ok) {
            WP_$CALLOC(&ppn, &local_status);
            if (local_status != status_$ok) {
                CRASH_SYSTEM(&local_status);
            }
            MMU_$INSTALL(ppn, va, 0x16);
        }

        /* Clear the ASTE */
        uint16_t *ptr = (uint16_t*)aste_ptr;
        for (j = 9; j >= 0; j--) {
            *ptr++ = 0;
        }

        /* Ensure segment map page is mapped */
        va = (uint32_t)((char*)segmap_ptr - 0x80);
        if (MMU_$VTOP(va, &local_status) == 0 && local_status != status_$ok) {
            WP_$CALLOC(&ppn, &local_status);
            if (local_status != status_$ok) {
                CRASH_SYSTEM(&local_status);
            }
            MMU_$INSTALL(ppn, va, 0x16);
        }

        /* Clear the segment map */
        ptr = (uint16_t*)((char*)segmap_ptr - 0x80 + 2);
        for (j = 0x3F; j >= 0; j--) {
            *(ptr - 1) = 0;
            ptr++;
        }

        /* Set segment index in ASTE */
        aste_ptr->seg_index = seg_index;

        ML_$LOCK(AST_LOCK_ID);

        /* Increment local ASTE count and free the entry */
        AST_$ASTE_L_CNT++;
        AST_$FREE_ASTE(aste_ptr);

        seg_index++;
    }

    AST_$SIZE_AST += add_count;
    ML_$UNLOCK(AST_LOCK_ID);

done:
    *status = local_status;
    return AST_$SIZE_AST;
}
