/*
 * AST_$GET_DTV - Get date/time value for an object
 *
 * Returns the modification date/time for an object identified by UID.
 *
 * Original address: 0x00e05476
 */

#include "ast.h"
#include "proc1/proc1.h"

/* Internal function prototypes */
extern aote_t* FUN_00e0209e(uid_t *uid);
extern aote_t* FUN_00e020fa(uid_t *uid, uint32_t unused, status_$t *status, int8_t flag);

void AST_$GET_DTV(uid_t *uid, uint32_t unused, uint32_t *dtv, status_$t *status)
{
    aote_t *aote;
    uid_t local_uid;
    status_$t local_status;

    /* Copy UID locally */
    local_uid.high = uid->high;
    local_uid.low = uid->low;
    local_status = status_$ok;

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    aote = FUN_00e0209e(&local_uid);

    if (aote == NULL) {
        /* AOTE not found - try to load it */
        aote = FUN_00e020fa(&local_uid, unused, &local_status, 0xFF);
        if (aote == NULL) {
            goto done;
        }
    } else {
        /* Mark as busy */
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* Get date/time from AOTE (at offset 0x38) */
    ML_$LOCK(PMAP_LOCK_ID);
    *dtv = *(uint32_t *)((char *)aote + 0x38);
    *((uint16_t *)(dtv + 1)) = *(uint16_t *)((char *)aote + 0x3C);
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Check if object is remote */
    if (*(int8_t *)((char *)aote + 0xB9) < 0) {
        local_status = status_$file_object_not_found;
    }

done:
    ML_$UNLOCK(AST_LOCK_ID);
    PROC1_$INHIBIT_END();

    *status = local_status;
}
