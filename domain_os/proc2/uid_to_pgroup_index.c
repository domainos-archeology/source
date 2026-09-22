/*
 * PROC2_$UID_TO_PGROUP_INDEX - Map a process-group UID to its table slot
 *
 * Re-emitted from the image (0x00E42272..0x00E422CA, 90 bytes) and
 * verified; the previous body was faithful.
 *
 *   00e4227a  clr.w D2w                     ; result 0
 *   00e42282  move.b (A2),D0b ; tst.w / bne ; first byte of uid.high
 *   00e42288  move.w (0x2,A2),(-0xe,A6)     ; synthetic: low word of uid.high
 *   00e42294  bsr PGROUP_FIND_BY_UPGID      ; (result slot) -> D2
 *   00e4229a  PROC2_$FIND_INDEX(uid, &status(-0x8)); tst.l / bne exit
 *   00e422bc  move.w (-0xd4,A1),D2w         ; entry+0x10
 *
 * Callers: SIGNAL_PGROUP_OS 0x00E3F30C, LIST_PGROUP 0x00E4023A,
 * SIGNAL_PGROUP 0x00E3F296.
 *
 * Original address: 0x00e42272
 */

#include "proc2/proc2_internal.h"

int16_t PROC2_$UID_TO_PGROUP_INDEX(uid_t *pgroup_uid)
{
    int16_t result = 0;          /* D2 */
    uint16_t upgid;              /* A6-0xE */
    status_$t status;            /* A6-0x8 */
    int16_t index;

    /* 0x00E42280-0x00E42286 */
    if (((pgroup_uid->high >> 24) & 0xFF) == 0) {
        upgid = (uint16_t)(pgroup_uid->high & 0xFFFF);      /* 0x00E42288 */
        result = PGROUP_FIND_BY_UPGID(upgid);                /* 0x00E42294 */
    } else {
        index = PROC2_$FIND_INDEX(pgroup_uid, &status);     /* 0x00E4229A-0x00E422A4 */
        if (status == status_$ok) {                          /* 0x00E422A6 */
            result = (int16_t)P2_INFO_ENTRY(index)->pgroup_table_idx;   /* 0x00E422BC */
        }
    }
    return result;                                           /* 0x00E422C0 */
}
