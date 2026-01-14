/*
 * AST_$SET_DTS - Set date/time stamps for an object
 *
 * Sets modification and/or access date/time stamps for an object.
 * The flags parameter controls which timestamps are updated.
 *
 * Flags:
 *   0x01 - Load AOTE if not found
 *   0x02 - Set modification time (DTV)
 *   0x04 - Set access time and creation time
 *   0x08 - Set DTM (date/time modified)
 *   0x10 - Use current time (ignore provided times)
 *
 * Original address: 0x00e05540
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "time/time.h"

uint8_t AST_$SET_DTS(uint16_t flags, uid_t *uid, uint32_t *dtv, uint32_t *access_time, status_$t *status)
{
    aote_t *aote;
    uid_t local_uid;
    status_$t local_status;
    uint8_t result;

    /* Copy UID locally */
    local_uid.high = uid->high;
    local_uid.low = uid->low;
    local_status = status_$ok;
    result = 0;

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    aote = FUN_00e0209e(&local_uid);

    if (aote == NULL && (flags & 0x01) != 0) {
        /* AOTE not found - try to load it */
        aote = FUN_00e020fa(&local_uid, 0, &local_status, 0xFF);
    }

    if (aote == NULL) {
        goto done;
    }

    /* Check if we should use current time */
    if ((flags & 0x10) != 0 && (aote->status_flags & 0x10) != 0) {
        /* Use current time */
        result = 0xFF;
        aote->flags &= ~AOTE_FLAG_TOUCHED;

        /* Skip if remote object */
        if (*(int8_t *)((char *)aote + 0xB9) < 0) {
            goto done;
        }

        ML_$LOCK(PMAP_LOCK_ID);
        TIME_$CLOCK((uint32_t *)((char *)aote + 0x30));
    } else {
        /* Set specific timestamps */
        ML_$LOCK(PMAP_LOCK_ID);

        if ((flags & 0x02) != 0) {
            /* Set DTV (modification time) at offset 0x38 */
            *(uint32_t *)((char *)aote + 0x38) = *dtv;
            *(uint16_t *)((char *)aote + 0x3C) = *((uint16_t *)(dtv + 1));
        }

        if ((flags & 0x04) != 0) {
            /* Set access time at offset 0x28 and creation time at 0x40 */
            *(uint32_t *)((char *)aote + 0x28) = *access_time;
            *(uint16_t *)((char *)aote + 0x2C) = *((uint16_t *)(access_time + 1));
            *(uint32_t *)((char *)aote + 0x40) = *access_time;
            *(uint16_t *)((char *)aote + 0x44) = *((uint16_t *)(access_time + 1));
        }

        result = 0;

        if ((flags & 0x08) != 0) {
            /* Set DTM at offset 0x30 */
            *(uint32_t *)((char *)aote + 0x30) = *access_time;
            *(uint16_t *)((char *)aote + 0x34) = *((uint16_t *)(access_time + 1));
        }
    }

    /* Mark AOTE as dirty */
    aote->flags |= AOTE_FLAG_DIRTY;
    ML_$UNLOCK(PMAP_LOCK_ID);

done:
    ML_$UNLOCK(AST_LOCK_ID);
    PROC1_$INHIBIT_END();

    *status = local_status;
    return result;
}
