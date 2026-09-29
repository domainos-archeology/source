/*
 * PROC2_$SET_SESSION_ID - Put the calling process into a session
 *
 * Re-emitted from the image (0x00E41C42..0x00E41D28, 232 bytes).
 *
 * Frame (link.w A6,-0x14; A5 = 0xE7BE84):
 *   (0x8,A6)  flags byte ptr -> D3b (bit 7: allow a group leader through)
 *   (0xC,A6)  session_id ptr -> D2
 *   (0x10,A6) status_ret <- A6-0x4 (cleared at 0x00E41C6A)
 *   A2 = the caller's entry (biased, mulu): (-0xCE) = +0x16 upid,
 *   (-0x88) = +0x5C session_id, (-0xD4) = +0x10 pgroup index
 *
 * Only reference: the SVC table entry at 0x00E7B8AE.
 *
 * Original address: 0x00e41c42
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_SESSION_ID(int8_t *flags, int16_t *session_id, status_$t *status_ret)
{
    int8_t flag_byte;            /* D3b */
    int16_t new_session;         /* D2 */
    int16_t pgroup_idx;          /* D0 */
    proc2_info_t *entry;         /* A2 */
    status_$t status;            /* A6-0x4 */

    /* 0x00E41C50-0x00E41C68 */
    flag_byte = *flags;
    new_session = *session_id;
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E41C6A-0x00E41C88 */
    status = status_$ok;
    entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);

    /* 0x00E41C8C: cmp.w (-0xce,A2),D2w */
    if ((uint16_t)new_session == entry->upid) {
        /* 0x00E41CAC: tst.w D2w / beq -> apply */
        if (new_session != 0) {
            /* 0x00E41CB0-0x00E41CB8 */
            pgroup_idx = PGROUP_FIND_BY_UPGID((uint16_t)new_session);
            /* 0x00E41CBA-0x00E41CC2: my group exists and IS that group */
            if (entry->pgroup_table_idx != 0 &&
                entry->pgroup_table_idx == (uint16_t)pgroup_idx) {
                /* 0x00E41CC4: tst.b D3b / bmi -> apply */
                if (flag_byte >= 0) {
                    status = status_$proc2_process_is_group_leader;   /* 0x00E41CC8 */
                }
            } else if (pgroup_idx != 0) {
                /* 0x00E41CD2-0x00E41CD6: someone else's group */
                status = status_$proc2_process_using_pgroup_id;
            }
        }
    } else {
        /* 0x00E41C92-0x00E41CAA: a non-zero foreign id while already in
         * a session and a group is refused */
        if (new_session != 0 && entry->session_id != 0 &&
            entry->pgroup_table_idx != 0) {
            status = status_$proc2_pgroup_in_different_session;
        }
    }

    /* 0x00E41CDE/0x00E41CE2: tst.l status / bne -> unlock */
    if (status == status_$ok) {
        /* 0x00E41CE4-0x00E41CF2: PGROUP_CLEANUP_INTERNAL(entry, 2) */
        PGROUP_CLEANUP_INTERNAL(entry, 2);
        /* 0x00E41CF4: entry+0x5C = new session */
        entry->session_id = (uint16_t)new_session;
        /* 0x00E41CF8-0x00E41D08: PGROUP_SET_INTERNAL(entry, new session, &status) */
        PGROUP_SET_INTERNAL(entry, (uint16_t)new_session, &status);
    }

    /* 0x00E41D0C-0x00E41D1C */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
