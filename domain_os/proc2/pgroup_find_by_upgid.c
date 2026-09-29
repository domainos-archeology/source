/*
 * PGROUP_FIND_BY_UPGID - Find the process-group slot holding a UPGID
 *
 * Re-emitted from the image (0x00E42224..0x00E42270, 78 bytes).
 *
 * Scans PROC2_$DATA.pgroup slots 1..70 (`moveq #0x45` against the dbf, A1 =
 * 0xEA551C + 8 + i*8, fields at (0x3F30,A1) ref_count and (0x3F34,A1)
 * upgid).  Free slots (ref_count 0) are skipped.  The comparison is a
 * 32-bit one between the SIGN-extended argument (`ext.l D4`) and the
 * ZERO-extended table word (`clr.l D3; move.w`), so a UPGID with bit 15
 * set can never match -- reproduced as found.
 *
 * Returns the slot index in D0, or 0.
 *
 * Callers: PROC2_$UID_TO_PGROUP_INDEX 0x00E42294, INIT_ENTRY_INTERNAL
 * 0x00E7334E, PGROUP_SET_INTERNAL 0x00E41EB8, 0x00E3FE66, SET_PGROUP
 * 0x00E4119E, SET_SESSION_ID 0x00E41CB4, PGROUP_INFO 0x00E41DFE.
 *
 * Original address: 0x00e42224
 */

#include "proc2/proc2_internal.h"

int16_t PGROUP_FIND_BY_UPGID(uint16_t upgid)
{
    int16_t i;
    int32_t wanted = (int32_t)(int16_t)upgid;    /* 0x00E4224E/0x00E42254 */

    /* 0x00E42236-0x00E42262: 70 iterations, i = 1..70 */
    for (i = 1; i <= 70; i++) {
        pgroup_entry_t *entry = &PROC2_$DATA.pgroup[i];

        /* 0x00E42246: tst.w ref_count / beq next */
        if (entry->ref_count == 0) {
            continue;
        }
        /* 0x00E4224C-0x00E42258: zero-extended upgid vs sign-extended arg */
        if ((int32_t)(uint32_t)entry->upgid == wanted) {
            return i;                                        /* 0x00E4225A */
        }
    }

    return 0;                                                /* 0x00E42266 */
}
