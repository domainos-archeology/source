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

#include "ast.h"
#include "proc1/proc1.h"

/* Internal function prototypes */
extern void FUN_00e0209e(uid_t *uid);  /* Look up AOTE by UID */
extern void FUN_00e020fa(uid_t *uid, uint16_t segment, status_$t *status, int8_t force);
extern aste_t* FUN_00e0250c(aote_t *aote, int16_t segment);
extern aste_t* FUN_00e0255c(aote_t *aote, int16_t segment, status_$t *status);
extern void FUN_00e01ad2(aote_t *aote, int8_t flags1, uint16_t flags2,
                         uint16_t flags3, status_$t *status);
extern void REM_FILE_$TRUNCATE(uid_t *vol_uid, uid_t *uid, uint32_t new_size,
                                uint16_t flags, uint8_t *result, status_$t *status);

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
    FUN_00e0209e(&local_uid);
    aote = NULL;  /* TODO: Get from FUN_00e0209e return in A0 */

    if (aote == NULL) {
        /* AOTE not cached - try to load it */
        FUN_00e020fa(&local_uid, 0, &local_status, 0);
        aote = NULL;  /* TODO: Get from FUN_00e020fa return in A0 */
        if (aote == NULL) {
            ML_$UNLOCK(AST_LOCK_ID);
            if (retry < 0 && local_status == status_$file_object_not_found) {
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
    uint32_t current_size = *(uint32_t *)((char *)aote + 0x20);

    if (new_size < current_size) {
        /* Truncating - free pages beyond new size */
        /* TODO: Implement page freeing logic */
        /* This involves iterating through segments and freeing pages */
    } else if (new_size > current_size && extend < 0) {
        /* Extending - may need to allocate disk blocks */
        /* TODO: Implement extension logic */
    }

    /* Update file size */
    *(uint32_t *)((char *)aote + 0x20) = new_size;

    /* Mark AOTE as dirty */
    aote->flags |= AOTE_FLAG_DIRTY;

    /* Flush if needed */
    if (truncate_to_zero < 0) {
        FUN_00e01ad2(aote, -1, 0, 0xFFE0, &local_status);
    }

    /* Clear in-transition flag */
    aote->flags &= ~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

    ML_$UNLOCK(AST_LOCK_ID);

done:
    PROC1_$INHIBIT_END();
    *status = local_status;
}
