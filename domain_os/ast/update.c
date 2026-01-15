/*
 * AST_$UPDATE - Periodic update of dirty objects and segments
 *
 * Scans through AOTEs and ASTEs, writing back dirty data to disk.
 * Used for background writeback of cached data.
 *
 * Original address: 0x00e016d0
 */

#include "ast/ast_internal.h"

void AST_$UPDATE(void)
{
    aote_t *aote;
    aste_t *aste;
    uint16_t aote_count;
    uint16_t aste_count;
    segmap_entry_t *segmap;
    status_$t status;

    /* Skip if diskless */
    if (NETWORK_$REALLY_DISKLESS < 0) {
        return;
    }

    ML_$LOCK(AST_LOCK_ID);

    aote_count = 0;
    aste_count = 0;
    aote = AST_$UPDATE_SCAN;

    while (1) {
        /* Check if AOTE is an area mapping and has local data to flush */
        if ((aote->status_flags & 0x1000) != 0 &&
            (int8_t)aote->flags >= 0 &&
            aote->ref_count == 0) {

            /* Scan ASTEs for this AOTE */
            for (aste = aote->aste_list; aste != NULL; aste = aste->next) {
                /* Check if ASTE is dirty and ready to flush */
                if ((int16_t)aste->flags >= 0 &&
                    (aste->flags & ASTE_FLAG_DIRTY) != 0 &&
                    aste->wire_count == 0 &&
                    aste->timestamp <= AST_$UPDATE_TIMESTAMP) {

                    /* Mark as in-transition */
                    aste->flags |= ASTE_FLAG_IN_TRANS;

                    ML_$UNLOCK(AST_LOCK_ID);

                    /* Calculate segment map address */
                    segmap = (segmap_entry_t *)((char *)SEGMAP_BASE +
                              (uint32_t)aste->seg_index * 0x80 - 0x80);

                    /* Write dirty pages */
                    ast_$update_aste(aste, segmap, 0, &status);

                    ML_$LOCK(AST_LOCK_ID);

                    /* Clear in-transition flag */
                    aste->flags &= ~ASTE_FLAG_IN_TRANS;

                    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

                    if (status != status_$ok) {
                        break;
                    }

                    aste_count++;
                    if (aste_count > 0x1F && aste->timestamp != 0) {
                        AST_$UPDATE_TIMESTAMP = aste->timestamp - 1;
                        goto done;
                    }
                }
            }

            /* Reset timestamp for next scan */
            AST_$UPDATE_TIMESTAMP = 0xFFFF;

            /* Flush AOTE if needed */
            if ((int8_t)aote->flags >= 0) {
                aote->flags |= AOTE_FLAG_IN_TRANS;
                ast_$purify_aote(aote, 0, &status);
                aote->flags &= ~AOTE_FLAG_IN_TRANS;
                EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
            }

            aote_count++;
        }

        /* Move to next AOTE */
        aote = (aote_t *)((char *)aote + 0xC0);

        if (aote >= AST_$AOTE_LIMIT) {
            /* Reached end - flush disk buffers if not diskless */
            if (NETWORK_$REALLY_DISKLESS >= 0) {
                ML_$UNLOCK(AST_LOCK_ID);
                DBUF_$UPDATE_VOL(0, &UID_$NIL);
                ML_$LOCK(AST_LOCK_ID);
            }
            /* Reset to start of AOTE area */
            aote = (aote_t *)0xEC7B60;
            break;
        }

        /* Limit iterations per call */
        if (aote_count >= 0x4B || aste_count > 0x1F) {
            break;
        }
    }

done:
    AST_$UPDATE_SCAN = aote;
    ML_$UNLOCK(AST_LOCK_ID);
}
