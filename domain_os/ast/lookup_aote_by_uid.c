/*
 * ast_$lookup_aote_by_uid - Look up an AOTE by its UID
 *
 * Searches the AOTE hash table for an entry matching the given UID.
 * If found and not in-transition, returns the AOTE pointer.
 * If found but in-transition, waits for transition to complete.
 * Returns NULL if not found.
 *
 * Original address: 0x00e0209e
 */

#include "ast/ast_internal.h"

/* Hash function for UID lookup */
extern uint16_t UID_$HASH(uid_t *uid, void *table_info);

/* Table info for UID hashing - at address 0xE01BEC in original */
#if defined(M68K)
#define AST_HASH_TABLE_INFO (*(void **)0xE01BEC)
#else
extern void *ast_hash_table_info;
#define AST_HASH_TABLE_INFO ast_hash_table_info
#endif

/*
 * AOTE hash table (256 entries)
 * Located at AST globals base (0xE1DC80)
 */
#if defined(M68K)
#define AST_AOTH_BASE ((aote_t **)0xE1DC80)
#else
extern aote_t **ast_aoth_base;
#define AST_AOTH_BASE ast_aoth_base
#endif

aote_t *ast_$lookup_aote_by_uid(uid_t *uid)
{
    uint16_t hash_index;
    aote_t *aote;

    /* Hash the UID to get the bucket index */
    hash_index = UID_$HASH(uid, &AST_HASH_TABLE_INFO);

    while (1) {
        /* Get the head of the hash chain */
        aote = AST_AOTH_BASE[hash_index];

        /* Walk the hash chain */
        while (aote != NULL) {
            /* Compare UIDs (at offset 0x10 and 0x14 in AOTE) */
            uint32_t *aote_uid = (uint32_t *)((char *)aote + 0x10);

            if (aote_uid[0] == uid->high && aote_uid[1] == uid->low) {
                /* UID matches - check if in-transition */
                /* Offset 0xBF is the flags byte, bit 7 is in-transition */
                if ((*((int8_t *)((char *)aote + 0xBF))) >= 0) {
                    /* Not in transition, return it */
                    return aote;
                }
                /* In transition - wait and retry */
                AST_$WAIT_FOR_AST_INTRANS();
                break;  /* Start over from hash lookup */
            }
            /* Move to next in chain */
            aote = aote->hash_next;
        }

        /* If we fell through without finding, return NULL */
        if (aote == NULL) {
            return NULL;
        }
    }
}
