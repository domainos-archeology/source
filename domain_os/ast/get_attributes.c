/*
 * AST_$GET_ATTRIBUTES - Get object attributes
 *
 * Retrieves the attributes for an object by UID. For remote objects,
 * fetches updated attributes from the network. For local objects,
 * returns cached attributes from the AOTE.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   flags - Lookup flags
 *   attrs - Output buffer for attributes (144 bytes)
 *   status - Status return
 *
 * Original address: 0x00e047a0
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"

void AST_$GET_ATTRIBUTES(uid_t *uid, uint16_t flags, void *attrs, status_$t *status)
{
    uint32_t *attr_buf = (uint32_t *)attrs;
    aote_t *aote;
    status_$t local_status[2];
    int i;

    /* Check for invalid flags */
    if ((flags & 0xFC00) != 0) {
        *status = status_$ast_incompatible_request;
        return;
    }

    /* Check for NIL UID */
    if (uid->high == UID_$NIL.high && uid->low == UID_$NIL.low) {
        *status = FUN_00e00be8(uid, 0x30F01);
        return;
    }

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    FUN_00e0209e(uid);
    aote = NULL;  /* TODO: Get from FUN_00e0209e return in A0 */

    if (aote == NULL) {
        /* AOTE not cached - try to load it */
        FUN_00e020fa(uid, 0, local_status, -((int8_t)flags < 0));
        *status = local_status[0];
        aote = NULL;  /* TODO: Get from FUN_00e020fa return in A0 */
        if (aote == NULL) {
            goto done;
        }
    } else {
        /* Mark AOTE as busy */
        aote->flags |= AOTE_FLAG_BUSY;
        *status = status_$ok;
    }

    /* Check if remote object and refresh requested */
    if ((flags & 0x20) != 0 && *(int8_t *)((char *)aote + 0xB9) < 0) {
        /* Remote object - fetch attributes from network */
        uint16_t net_flags;

        aote->flags |= AOTE_FLAG_IN_TRANS;
        ML_$UNLOCK(AST_LOCK_ID);

        /* Determine network request flags */
        if ((*(uint8_t *)((char *)aote + 0x0F) & 1) == 0) {
            if ((flags & 0x200) != 0 || (*(uint16_t *)((char *)aote + 0xBE) & 0x10) != 0) {
                net_flags = 0x88;
            } else {
                net_flags = 0x08;
            }
        } else {
            net_flags = 0x08;
        }

        /* Clear touched flag */
        aote->flags &= ~AOTE_FLAG_TOUCHED;

        NETWORK_$AST_GET_INFO((void *)((char *)aote + 0x9C), &net_flags, attrs, local_status);
        *status = local_status[0];

        ML_$LOCK(AST_LOCK_ID);

        if (*status == status_$ok) {
            ML_$LOCK(PMAP_LOCK_ID);

            /* Update file size - keep larger of cached and fetched */
            uint32_t cached_size = *(uint32_t *)((char *)aote + 0x20);
            if (cached_size <= attr_buf[5]) {
                cached_size = attr_buf[5];
            }

            /* Save timestamps before overwrite */
            uint32_t saved_time = *(uint32_t *)((char *)aote + 0x38);
            uint16_t saved_sub = *(uint16_t *)((char *)aote + 0x3C);

            /* Copy attributes (144 bytes = 36 uint32_t) */
            uint32_t *attr_dest = (uint32_t *)((char *)aote + 0x0C);
            for (i = 0; i < 36; i++) {
                attr_dest[i] = attr_buf[i];
            }

            /* Restore file size and timestamps */
            *(uint32_t *)((char *)aote + 0x20) = cached_size;
            *(uint32_t *)((char *)aote + 0x38) = saved_time;
            *(uint16_t *)((char *)aote + 0x3C) = saved_sub;

            ML_$UNLOCK(PMAP_LOCK_ID);
        }

        aote->flags &= ~AOTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    } else {
        /* Local object or no refresh - return cached attributes */
        if (*(int8_t *)((char *)aote + 0xB9) >= 0) {
            /* Local object - copy attributes */
            uint32_t *attr_src = (uint32_t *)((char *)aote + 0x0C);
            for (i = 0; i < 36; i++) {
                attr_buf[i] = attr_src[i];
            }
        }
    }

done:
    ML_$UNLOCK(AST_LOCK_ID);
    PROC1_$INHIBIT_END();
}
