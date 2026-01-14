/*
 * AST_$INVALIDATE - Invalidate pages for an object
 *
 * Invalidates a range of pages for the specified object. Used when
 * the underlying data has been modified externally (e.g., by network
 * operations) and the cached pages need to be refreshed.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   start_page - Starting page number
 *   count - Number of pages to invalidate
 *   flags - Operation flags (negative = wait for completion)
 *   status - Status return
 *
 * Original address: 0x00e0662e
 */

#include "ast.h"
#include "proc1/proc1.h"

/* Internal function prototypes */
extern void FUN_00e0209e(uid_t *uid);  /* Look up AOTE by UID */
extern void FUN_00e020fa(uid_t *uid, uint16_t segment, status_$t *status, int8_t force);
extern status_$t FUN_00e062fa(uint16_t end_page);  /* Invalidate with wait */
extern void FUN_00e064b0(uint16_t end_page);       /* Invalidate without wait */
extern void REM_FILE_$INVALIDATE(uid_t *vol_uid, uid_t *uid, uint32_t start,
                                  uint32_t count, int8_t flags, status_$t *status);

void AST_$INVALIDATE(uid_t *uid, uint32_t start_page, uint32_t count,
                     int16_t flags, status_$t *status)
{
    aote_t *aote;
    int8_t is_remote;
    uint32_t end_page;
    uint32_t file_end_page;
    uid_t vol_uid;

    *status = status_$ok;

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    FUN_00e0209e(uid);
    aote = NULL;  /* TODO: Get from FUN_00e0209e return in A0 */

    if (aote == NULL) {
        /* AOTE not cached - try to load it */
        FUN_00e020fa(uid, 0, status, 0);
        aote = NULL;  /* TODO: Get from FUN_00e020fa return in A0 */
        if (aote == NULL) {
            ML_$UNLOCK(AST_LOCK_ID);
            goto done;
        }
    } else {
        /* Mark AOTE as busy */
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* Check if remote object */
    is_remote = *(int8_t *)((char *)aote + 0xB9);

    /* Check if file has any pages */
    uint32_t file_size = *(uint32_t *)((char *)aote + 0x20);
    if (file_size != 0 && start_page <= ((file_size - 1) >> 10)) {
        /* Calculate end page */
        end_page = start_page + count - 1;
        file_end_page = (file_size - 1) >> 10;
        if (end_page > file_end_page) {
            end_page = file_end_page;
        }

        /* Mark AOTE as in transition */
        aote->flags |= AOTE_FLAG_IN_TRANS;

        if (flags < 0) {
            /* Wait for completion */
            *status = FUN_00e062fa((uint16_t)end_page);
        } else {
            /* Don't wait */
            FUN_00e064b0((uint16_t)end_page);
        }

        /* Clear in-transition flag */
        aote->flags &= ~AOTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    }

    /* Save volume UID before releasing lock */
    vol_uid.high = *(uint32_t *)((char *)aote + 0xAC);
    vol_uid.low = *(uint32_t *)((char *)aote + 0xB0);

    ML_$UNLOCK(AST_LOCK_ID);

    /* If remote object and no error, propagate invalidation */
    if (is_remote < 0 && *status == status_$ok) {
        REM_FILE_$INVALIDATE(&vol_uid, uid, start_page, count,
                             (int8_t)flags, status);
    }

done:
    PROC1_$INHIBIT_END();
}
