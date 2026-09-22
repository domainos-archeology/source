/*
 * AST_$GET_DTV - Return an object's DTV (date-time verified) clock
 *
 * Finds the object's AOTE (activating it with `force` = TRUE when it is
 * not resident), copies aote+0x38/+0x3C into `dtv` under the PMAP lock,
 * and reports file_$object_not_found for a remote object.
 *
 * Parameters (frame at 0x00E05476, `link.w A6,-0x10`):
 *   uid      (0x08,A6)  copied to (-0x8,A6) before use
 *   location (0x0C,A6)  longword forwarded to ast_$force_activate_segment
 *   dtv      (0x10,A6)  a 48-bit clock_t out (`move.l` + `move.w (0x4,A0)`);
 *                       the public prototype keeps the callers' uint32_t *
 *   status   (0x14,A6)  (A3); the local at (-0xC,A6) is copied out
 *
 * Original address: 0x00E05476 (202 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"

void AST_$GET_DTV(uid_t *uid, uint32_t location, uint32_t *dtv,
                  status_$t *status)
{
    clock_t *out = (clock_t *)dtv;      /* A0 at 0x00E054F4 */
    aote_t *aote;                       /* A2 */
    uid_t local_uid;                    /* (-0x8,A6) */
    status_$t local_status;             /* (-0xC,A6) */

    /* 0x00E05488..0x00E05494 */
    local_uid.high = uid->high;
    local_uid.low = uid->low;
    local_status = status_$ok;

    /* 0x00E05498..0x00E054AA */
    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E054AC..0x00E054BC */
    aote = ast_$lookup_aote_by_uid(&local_uid);
    if (aote == NULL) {
        /* 0x00E054BE..0x00E054DE: `st -(SP)` is force = TRUE */
        aote = ast_$force_activate_segment(&local_uid, location,
                                           &local_status, -1);
        if (aote == NULL) {
            goto done;
        }
    } else {
        /* 0x00E054E0: bset.b #0x6,(0xbf,A2) */
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* 0x00E054E6..0x00E0550E */
    ML_$LOCK(PMAP_LOCK_ID);
    out->high = aote->dtv_high;
    out->low = aote->dtv_low;
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* 0x00E05510..0x00E05516 */
    if (aote->remote_flag < 0) {
        local_status = file_$object_not_found;      /* 0xF0001 */
    }

done:
    /* 0x00E0551E..0x00E05532 */
    ML_$UNLOCK(AST_LOCK_ID);
    PROC1_$INHIBIT_END();
    *status = local_status;
}
