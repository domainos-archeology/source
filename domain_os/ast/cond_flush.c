/*
 * AST_$COND_FLUSH - Conditionally flush an object if timestamps differ
 *
 * Flushes dirty pages for an object only if the object's data timestamp
 * differs from the provided timestamp. This is used for conditional
 * synchronization where we only want to flush if changes have occurred.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   timestamp - Pointer to timestamp to compare (6 bytes: 4-byte time + 2-byte sub)
 *   status - Status return
 *
 * Original address: 0x00e05b9c
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"

void AST_$COND_FLUSH(uid_t *uid, uint32_t *timestamp, status_$t *status)
{
    aote_t *aote;
    status_$t local_status;
    uid_t local_uid;

    /* Copy UID locally */
    local_uid.high = uid->high;
    local_uid.low = uid->low;

    local_status = status_$ok;

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    ast_$lookup_aote_by_uid(&local_uid);
    aote = NULL;  /* TODO: Get from ast_$lookup_aote_by_uid return in A0 */

    if (aote != NULL) {
        /* Compare timestamps */
        /* AOTE timestamp is at offset 0x38 (4 bytes) and 0x3c (2 bytes) */
        uint32_t aote_time = *(uint32_t *)((char *)aote + 0x38);
        uint16_t aote_sub = *(uint16_t *)((char *)aote + 0x3C);

        if (aote_time != timestamp[0] ||
            aote_sub != *(uint16_t *)((char *)timestamp + 4)) {
            /* Timestamps differ - flush the object */
            ast_$process_aote(aote, -1, 0, 0xFF00 | 0xE0, &local_status);

            if (local_status == status_$ok) {
                ast_$release_aote(aote);
            }
        }
    }

    ML_$UNLOCK(AST_LOCK_ID);
    PROC1_$INHIBIT_END();

    *status = local_status;
}
