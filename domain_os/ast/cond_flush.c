/*
 * AST_$COND_FLUSH - Flush an object only if its DTV differs
 *
 * Looks the object up in the active object table and, when its cached
 * DTV (aote+0x38/+0x3C, the 48-bit "date-time verified" clock) is not
 * equal to the caller's clock, deactivates it with ast_$process_aote
 * (purge = TRUE, keep = FALSE, wait = TRUE) and releases the AOTE if that
 * succeeded.  An object that is not active, or whose DTV already matches,
 * is left alone and the status stays status_$ok.
 *
 * Parameters (frame at 0x00E05B9C, `link.w A6,-0x10`):
 *   uid       (0x8,A6)  object UID, copied to (-0x8,A6) before the lookup
 *   timestamp (0xC,A6)  a 48-bit clock_t: `cmp.l (A3)` against aote+0x38
 *                       and `cmp.w (0x4,A3)` against aote+0x3C (A3).  The
 *                       public prototype keeps the `uint32_t *` shape the
 *                       FILE callers use.
 *   status    (0x10,A6) result; the local cell at (-0xC,A6) is copied out
 *
 * Original address: 0x00E05B9C (164 bytes), A5 = 0xE1DC80 (AST_ block;
 * nothing in it is touched here).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"

void AST_$COND_FLUSH(uid_t *uid, uint32_t *timestamp, status_$t *status)
{
    const clock_t *ts = (const clock_t *)timestamp;   /* A3 */
    aote_t *aote;                                     /* A2 */
    status_$t local_status;                           /* (-0xC,A6) */
    uid_t local_uid;                                  /* (-0x8,A6) */

    /* 0x00E05BAE..0x00E05BBA: two post-increment longword moves */
    local_uid.high = uid->high;
    local_uid.low = uid->low;
    local_status = status_$ok;

    /* 0x00E05BBE..0x00E05BD0 */
    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E05BD2..0x00E05BE2 */
    aote = ast_$lookup_aote_by_uid(&local_uid);
    if (aote != NULL) {
        /* 0x00E05BE4..0x00E05BF4: skip when both halves match */
        if (aote->dtv_high != ts->high || aote->dtv_low != ts->low) {
            /*
             * 0x00E05BF6..0x00E05C08: `st -(SP)` (wait), `clr.w -(SP)`
             * (keep), `st -(SP)` (purge) - three single bytes.
             */
            ast_$process_aote(aote, -1, 0, -1, &local_status);

            /* 0x00E05C0C..0x00E05C18 */
            if (local_status == status_$ok) {
                ast_$release_aote(aote);
            }
        }
    }

    /* 0x00E05C1A..0x00E05C2E */
    ML_$UNLOCK(AST_LOCK_ID);
    PROC1_$INHIBIT_END();

    /* 0x00E05C2E..0x00E05C32 */
    *status = local_status;
}
