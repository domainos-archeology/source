/*
 * PROC2_$PGUID_TO_UPGID - Convert a process-group UID to its UPGID
 *
 * Re-emitted from the image (0x00E41072..0x00E410C6, 86 bytes) together
 * with its module-local helper PROC2_$UID_TO_UPGID_INTERNAL
 * (0x00E422CC..0x00E42328, 94 bytes; sole caller 0x00E4109E).
 *
 * Frame (link.w A6,-0xC; A5 = 0xE7BE84): (0x8,A6) pgroup_uid copied to
 * A6-0x8, (0xC,A6) upgid_ret, (0x10,A6) status_ret (always status_$ok).
 *
 * Only reference: the SVC table entry at 0x00E7B87E.
 *
 * Original address: 0x00e41072
 */

#include "proc2/proc2_internal.h"

/*
 * PROC2_$UID_TO_UPGID_INTERNAL - 0x00E422CC
 *
 *   00e422d4  clr.w D2w                     ; result = 0
 *   00e422dc  move.b (A2),D0b               ; byte 0 of the UID (bits 31..24 of high)
 *   00e422de  tst.w D0w / bne               ; non-zero: a real process UID
 *   00e422e2  move.w (0x2,A2),D2w           ; zero: synthetic -> low word of high
 *   00e422e8  PROC2_$FIND_INDEX(uid, &status(-0x8)); tst.l status / bne exit
 *   00e4230a  move.w (-0xd4,A1),D0w         ; entry+0x10 pgroup index; beq exit
 *   00e4231a  move.w (0x3f34,A1),D2w        ; PGROUP_TABLE[idx].upgid
 *   00e4231e  move.w D2w,D0w                ; return
 */
static uint16_t PROC2_$UID_TO_UPGID_INTERNAL(uid_t *uid)
{
    uint16_t upgid = 0;          /* D2 */
    status_$t status;            /* A6-0x8 */
    int16_t index;
    uint16_t pgroup_idx;

    /* 0x00E422DA-0x00E422E0: the UID's first byte */
    if (((uid->high >> 24) & 0xFF) == 0) {
        upgid = (uint16_t)(uid->high & 0xFFFF);              /* 0x00E422E2 */
    } else {
        index = PROC2_$FIND_INDEX(uid, &status);             /* 0x00E422E8-0x00E422F2 */
        if (status == status_$ok) {                          /* 0x00E422F4: tst.l */
            pgroup_idx = P2_INFO_ENTRY(index)->pgroup_table_idx;   /* 0x00E4230A */
            if (pgroup_idx != 0) {
                upgid = PGROUP_ENTRY(pgroup_idx)->upgid;     /* 0x00E4231A */
            }
        }
    }
    return upgid;
}

void PROC2_$PGUID_TO_UPGID(uid_t *pgroup_uid, uint16_t *upgid_ret, status_$t *status_ret)
{
    uid_t uid;                   /* A6-0x8 */
    uint16_t upgid;              /* D2 */

    /* 0x00E41080-0x00E41088 */
    uid.high = pgroup_uid->high;
    uid.low = pgroup_uid->low;

    /* 0x00E4108C-0x00E41098 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E4109A-0x00E410A4: pea &uid, result in D0 (no result slot) */
    upgid = PROC2_$UID_TO_UPGID_INTERNAL(&uid);

    /* 0x00E410A6-0x00E410AC */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E410B2-0x00E410BC */
    *upgid_ret = upgid;
    *status_ret = status_$ok;
}
