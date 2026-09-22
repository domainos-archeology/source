/*
 * AST_$SET_DTS - Set an object's clocks
 *
 * With the object active (activated with force = TRUE when flags bit 0
 * asks for it): when flags bit 4 is set and the AOTE is TOUCHED, the
 * TOUCHED bit is cleared and TRUE returned; a remote object is then left
 * alone, a local one gets its DTU stamped from TIME_$CLOCK.  Otherwise
 * bit 1 stores *dtv as the DTV (0x38), bit 2 stores *time as both the DTM
 * (0x28) and the DTA (0x40), bit 3 stores *time as the DTU (0x30).  Both
 * arms mark the AOTE dirty under the PMAP lock.
 *
 * Parameters (frame at 0x00E05540, `link.w A6,-0x18`):
 *   flags  (0x08,A6)  word (D3)
 *   uid    (0x0A,A6)  copied to (-0x8,A6)
 *   dtv    (0x0E,A6)  48-bit clock (`move.l` + `move.w (0x4,A0)`)
 *   time   (0x12,A6)  48-bit clock (A3)
 *   status (0x16,A6)  from (-0xC,A6)
 * Returns D0b (D2b): TRUE only on the bit-4 path.
 *
 * Original address: 0x00E05540 (314 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "time/time.h"

uint8_t AST_$SET_DTS(uint16_t flags, uid_t *uid, uint32_t *dtv,
                     uint32_t *access_time, status_$t *status)
{
    const clock_t *dtv_in = (const clock_t *)dtv;
    const clock_t *time_in = (const clock_t *)access_time;    /* A3 */
    aote_t *aote;               /* A2 */
    uid_t local_uid;            /* (-0x8,A6) */
    status_$t local_status;     /* (-0xC,A6) */
    uint32_t location;          /* (-0x10,A6) */
    uint8_t result;             /* D2b */

    /* 0x00E05556..0x00E05566 */
    local_uid.high = uid->high;
    local_uid.low = uid->low;
    local_status = status_$ok;
    result = 0;

    /* 0x00E05568..0x00E0557A */
    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E0557C..0x00E055AE: btst.l D2,D3 with D2 = 0 is flags bit 0;
     * `st -(SP)` is force = TRUE */
    aote = ast_$lookup_aote_by_uid(&local_uid);
    if (aote == NULL && (flags & 0x01)) {
        location = 0;
        aote = ast_$force_activate_segment(&local_uid, location,
                                           &local_status, -1);
    }
    /* 0x00E055B0 */
    if (aote == NULL) {
        goto done;
    }

    /* 0x00E055B8..0x00E055C6: flags bit 4 and aote flags bit 4 (TOUCHED;
     * move.w (0xbe,A2) / btst.l #4) */
    if ((flags & 0x10) && (aote->flags & AOTE_FLAG_TOUCHED)) {
        /* 0x00E055C8..0x00E055EE */
        result = 0xFF;
        aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;
        if (aote->remote_flag < 0) {
            goto done;                              /* no DIRTY, no lock */
        }
        ML_$LOCK(PMAP_LOCK_ID);
        TIME_$CLOCK((clock_t *)&aote->dtu_high);    /* pea (0x30,A2) */
    } else {
        /* 0x00E055F2..0x00E05638 */
        ML_$LOCK(PMAP_LOCK_ID);
        if (flags & 0x02) {
            aote->dtv_high = dtv_in->high;
            aote->dtv_low = dtv_in->low;
        }
        if (flags & 0x04) {
            aote->dtm_high = time_in->high;
            aote->dtm_low = time_in->low;
            aote->dta_high = time_in->high;
            aote->dta_low = time_in->low;
        }
        if (flags & 0x08) {
            aote->dtu_high = time_in->high;
            aote->dtu_low = time_in->low;
        }
    }

    /* 0x00E0563E..0x00E05650 */
    aote->flags |= AOTE_FLAG_DIRTY;
    ML_$UNLOCK(PMAP_LOCK_ID);

done:
    /* 0x00E05652..0x00E0566E */
    ML_$UNLOCK(AST_LOCK_ID);
    PROC1_$INHIBIT_END();
    *status = local_status;
    return result;
}
