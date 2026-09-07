/*
 * AST_$TRUNCATE - Truncate an object to a new size
 *
 * Truncates or extends an object to the specified size. For truncation,
 * frees pages beyond the new size. For extension, may need to allocate
 * new disk blocks. Handles both local and remote objects.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   new_size - New size in bytes
 *   flags - Operation flags (bit 0: truncate to 0, bit 1: extend)
 *   result - Output: result byte
 *   status - Status return
 *
 * Original address: 0x00e05c40
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "rem_file/rem_file.h"

void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   uint8_t *result, status_$t *status)
{
    aote_t *aote;
    aste_t *aste;
    uid_t local_uid;
    uid_t vol_uid;
    status_$t local_status;
    int8_t truncate_to_zero;
    int8_t extend;
    int8_t retry;

    /* Copy UID locally (mask off high bit of low word) */
    local_uid.high = uid->high;
    local_uid.low = uid->low & 0xFEFFFFFF;

    truncate_to_zero = -((flags & 1) != 0);
    extend = -((flags & 2) != 0);
    retry = 0;

    *result = 0;
    local_status = status_$ok;

    if (truncate_to_zero < 0) {
        new_size = 0;
    }

    PROC1_$INHIBIT_BEGIN();

retry_loop:
    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    aote = ast_$lookup_aote_by_uid(&local_uid);

    if (aote == NULL) {
        /* AOTE not cached - try to load it */
        aote = ast_$force_activate_segment(&local_uid, 0, &local_status, 0);
        if (aote == NULL) {
            ML_$UNLOCK(AST_LOCK_ID);
            if (retry < 0 && local_status == file_$object_not_found) {
                local_status = status_$ok;
            }
            goto done;
        }
    } else {
        /* Mark AOTE as busy */
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* Check if remote object */
    if (*(int8_t *)((char *)aote + 0xB9) < 0) {
        /* Remote object - forward to server */
        vol_uid.high = *(uint32_t *)((char *)aote + 0xAC);
        vol_uid.low = *(uint32_t *)((char *)aote + 0xB0);
        ML_$UNLOCK(AST_LOCK_ID);
        REM_FILE_$TRUNCATE(&vol_uid, uid, new_size, flags, result, &local_status);
        goto done;
    }

    /* Local object - mark as in transition */
    aote->flags |= AOTE_FLAG_IN_TRANS;

    /* Get current file size */
    uint32_t current_size = aote->length;   /* 0x00E0607C */

    /*
     * TODO(source-22c): the two size-change bodies of AST_$TRUNCATE
     * (0x00E05C40, 1722 bytes) are not decompiled.  Missing on the
     * shrink path: the walk over the segments beyond the new end of file
     * that deactivates or flushes each ASTE, frees its pages through
     * AST_$FREE_PAGES and releases the disk blocks through the BAT; and on
     * the grow path (taken only when `extend` is TRUE): the block
     * allocation and the segment-map fill-in.  Only the AOTE length store
     * and the dirty marking below are present.  Tracked by bead source-22c
     * ("Complete AST subsystem logic (purification, truncation, bounds
     * checking)").
     */
    if (new_size < current_size) {
        /* shrink: page-freeing body not decompiled (see above) */
    } else if (new_size > current_size && extend < 0) {
        /* grow: block-allocation body not decompiled (see above) */
    }

    /* Update file size */
    aote->length = new_size;                /* 0x00E06098 */

    /* Mark AOTE as dirty */
    aote->flags |= AOTE_FLAG_DIRTY;

    /* Flush if needed */
    if (truncate_to_zero < 0) {
        ast_$process_aote(aote, -1, 0, 0xFFE0, &local_status);
    }

    /* Clear in-transition flag */
    aote->flags &= ~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

    ML_$UNLOCK(AST_LOCK_ID);

done:
    PROC1_$INHIBIT_END();
    *status = local_status;
}
