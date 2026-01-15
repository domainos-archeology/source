/*
 * ast_$process_aote - Process/deactivate an AOTE
 *
 * Attempts to deactivate an AOTE by freeing all its ASTEs and
 * purifying the object. Used during dismount and cache cleanup.
 *
 * Parameters:
 *   aote - The AOTE to process
 *   flags1 - Processing flags (negative = skip purify)
 *   flags2 - Additional flags (negative = allow deactivation of system objects)
 *   flags3 - Wait flags (negative = wait for in-transition)
 *   status - Output status
 *
 * Returns: Non-zero status bits on failure
 *
 * Original address: 0x00e01ad2
 */

#include "ast/ast_internal.h"

/* External function prototypes */

/* Status codes */
#define status_$ast_segment_not_deactivatable 0x00030004

/* Hash table info */
#if defined(M68K)
#define AST_HASH_TABLE_INFO (*(void **)0xE01BEC)
#define AST_AOTH_BASE ((aote_t **)0xE1DC80)
#else
#define AST_HASH_TABLE_INFO ast_hash_table_info
#define AST_AOTH_BASE ast_aoth_base
#endif

uint16_t ast_$process_aote(aote_t *aote, uint8_t flags1, uint16_t flags2,
                           uint16_t flags3, status_$t *status)
{
    uint8_t busy_or_intrans;
    aste_t *aste;
    uint16_t hash_index;
    aote_t *hash_entry;

    *status = status_$ok;

    /* Check if AOTE is busy (ref_count != 0) or in-transition */
    busy_or_intrans = (aote->ref_count != 0) | (aote->flags & AOTE_FLAG_IN_TRANS ? 0xFF : 0);

    if ((int8_t)busy_or_intrans < 0) {
        /* Already busy or in-transition */
        *status = status_$ast_segment_not_deactivatable;
        return (uint16_t)busy_or_intrans;
    }

    /* Check if this is a system object that can't be deactivated */
    if ((int16_t)flags2 >= 0) {
        /* Object type at offset 0x0D */
        uint8_t obj_type = *((uint8_t *)aote + 0x0D);
        if (obj_type == 2) {  /* System object */
            /* Check if it has special attributes (offset 0x0F bit 1) */
            if ((*((uint8_t *)aote + 0x0F) & 2) != 0) {
                /* Not remote? */
                if (*((int8_t *)aote + 0xB9) >= 0) {
                    *status = status_$ast_segment_not_deactivatable;
                    return (uint16_t)busy_or_intrans;
                }
            }
        }
    }

    /* Mark AOTE as in-transition */
    aote->flags |= AOTE_FLAG_IN_TRANS;

    /* Process all ASTEs attached to this AOTE */
    while (aote->aste_list != NULL) {
        aste = aote->aste_list;

        /* Check if ASTE is in-transition and we should wait */
        if ((int16_t)aste->flags < 0 && (int16_t)flags3 < 0) {
            AST_$WAIT_FOR_AST_INTRANS();
            continue;
        }

        /* Process/free the ASTE */
        FUN_00e01950(aste, ((uint32_t)flags1 << 24) | ((uint32_t)flags2 & 0xFF00),
                     status);

        if (*status != status_$ok) {
            if (*status == status_$ast_segment_not_deactivatable) {
                goto restore_and_return;
            }
            goto set_error_and_return;
        }

        /* Free the ASTE */
        AST_$FREE_ASTE(aste);
    }

    /* If flags1 < 0, skip purification */
    if ((int8_t)flags1 < 0) {
        goto remove_from_hash;
    }

    /* Purify the AOTE */
    ast_$purify_aote(aote, 0, status);
    if (*status != status_$ok) {
        goto set_error_and_return;
    }

remove_from_hash:
    /* Remove AOTE from hash table */
    /* Hash using the original UID stored at offset 0xA4 */
    hash_index = UID_$HASH((uid_t *)((char *)aote + 0xA4), (uint16_t *)AST_HASH_TABLE_INFO);
    hash_entry = AST_AOTH_BASE[hash_index];

    if (hash_entry == aote) {
        /* AOTE is at head of chain */
        AST_AOTH_BASE[hash_index] = aote->hash_next;
    } else {
        /* Find AOTE in chain */
        while (hash_entry->hash_next != aote) {
            hash_entry = hash_entry->hash_next;
        }
        hash_entry->hash_next = aote->hash_next;
    }
    return (uint16_t)busy_or_intrans;

set_error_and_return:
    /* Set high bit on status to indicate error */
    *(uint8_t *)status |= 0x80;

restore_and_return:
    /* Clear in-transition flag */
    aote->flags &= ~AOTE_FLAG_IN_TRANS;

    /* Signal completion */
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    return (uint16_t)busy_or_intrans;
}
