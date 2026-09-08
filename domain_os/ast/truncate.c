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
 *   result - Output: a Domain BOOLEAN byte.  0x00E05C6A-0x00E05C70
 *            `clr.b D3b` / `move.b D3b,(A0)` clears it on entry and
 *            0x00E05DB6 `st (A1)` sets it on the local path.
 *   status - Status return
 *
 * Original address: 0x00e05c40
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "rem_file/rem_file.h"

void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   boolean *result, status_$t *status)
{
    aote_t *aote;
    aste_t *aste;
    uid_t local_uid;
    uid_t vol_uid;
    status_$t local_status;
    int8_t truncate_to_zero;
    int8_t extend;
    int8_t retry;
    /*
     * A6-0x40: the six-byte clock cell REM_FILE_$TRUNCATE fills in.
     * 0x00E06250 `pea (-0x40,A6)` passes THIS local as the remote call's
     * fifth argument - not the caller's `result` byte - and 0x00E062A6 /
     * 0x00E062B2 copy it back into the AOTE as `move.l (-0x40,A6),(0x40,A2)`
     * / `move.w (-0x3c,A6),(0x44,A2)` and the same pair at +0x28/+0x2C.
     */
    clock_t remote_dtm;

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
        /*
         * 0x00E0624A-0x00E06262, right to left:
         *   subq.l #0x2,SP           ; discarded word result slot
         *   move.l (0x16,A6),-(SP)   ; the CALLER's status pointer
         *   pea (-0x40,A6)           ; &remote_dtm, not `result`
         *   move.b D7b,-(SP)         ; flags, one byte
         *   move.l (0xc,A6),-(SP)    ; new_size
         *   pea (-0x8,A6)            ; &local_uid
         *   pea (-0xc8,A6)           ; &vol_uid
         *
         * TODO(source-2ih2): the remote path also reports through the
         * caller's status pointer directly and copies remote_dtm into the
         * AOTE at +0x28/+0x2C and +0x40/+0x44 (0x00E06270-0x00E062CC);
         * neither is modelled here yet.
         */
        REM_FILE_$TRUNCATE(&vol_uid, uid, new_size, flags, &remote_dtm,
                           &local_status);
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
        /* 0x00E06138-0x00E06142 (and the same at 0x00E061DE): `st -(SP)`
         * (flags3), `clr.w -(SP)` (flags2), `st -(SP)` (flags1).
         * (source-o7gq) */
        ast_$process_aote(aote, -1, 0, -1, &local_status);
    }

    /* Clear in-transition flag */
    aote->flags &= ~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

    ML_$UNLOCK(AST_LOCK_ID);

done:
    PROC1_$INHIBIT_END();
    *status = local_status;
}
