/*
 * PROC2_$UPGID_TO_UID - Build the synthetic UID for a Unix process group
 *
 * Re-emitted from the image (0x00E4100C..0x00E41070, 102 bytes) together
 * with PROC2_$UPGID_TO_UID_INTERNAL (0x00E4232A..0x00E42354, 44 bytes).
 *
 * Frame (link.w A6,-0x14; A5 = 0xE7BE84): (0x8,A6) upgid ptr -> A6-0xA,
 * (0xC,A6) uid_ret, (0x10,A6) status_ret (always status_$ok).  The helper
 * is called with (pea (-0x14,A6), move.w upgid) and cleans 6 bytes; its
 * result is copied to A6-0x8 and then to *uid_ret after the unlock.
 *
 * Only references: 0x00E1C19A and the SVC table entry at 0x00E7B792.
 *
 * Original address: 0x00e4100c
 */

#include "proc2/proc2_internal.h"

/*
 * PROC2_$UPGID_TO_UID_INTERNAL - 0x00E4232A
 *
 *   00e4232e  movea.l #0xe1737c,A0          ; UID_$NIL
 *   00e42338  move.l (A0)+,(-0x10,A6) ; move.l (A0)+,(-0xc,A6)
 *   00e42340  move.w D0w,(-0xe,A6)          ; low word of .high = upgid
 *   00e4234c  copy the eight bytes to *uid_ret
 *
 * Frame: (0x8,A6) uid_ret, (0xC,A6) upgid word.
 */
static void PROC2_$UPGID_TO_UID_INTERNAL(uid_t *uid_ret, uint16_t upgid)
{
    uid_t local;                 /* A6-0x10 */

    local.high = UID_$NIL.high;
    local.low = UID_$NIL.low;
    local.high = (local.high & 0xFFFF0000u) | upgid;         /* 0x00E42340 */
    uid_ret->high = local.high;
    uid_ret->low = local.low;
}

void PROC2_$UPGID_TO_UID(uint16_t *upgid, uid_t *uid_ret, status_$t *status_ret)
{
    uint16_t upgid_val;          /* A6-0xA */
    uid_t built;                 /* A6-0x14 */
    uid_t copy;                  /* A6-0x8 */

    /* 0x00E41018-0x00E4101E */
    upgid_val = *upgid;

    /* 0x00E41022-0x00E4102E */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E41030-0x00E4103C */
    PROC2_$UPGID_TO_UID_INTERNAL(&built, upgid_val);

    /* 0x00E4103E-0x00E41046 */
    copy.high = built.high;
    copy.low = built.low;

    /* 0x00E4104A-0x00E41050 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E41056-0x00E41068 */
    uid_ret->high = copy.high;
    uid_ret->low = copy.low;
    *status_ret = status_$ok;
}
