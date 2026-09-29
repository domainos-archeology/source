/*
 * PROC2_$SIGNAL_PGROUP_INTERNAL - Signal every member of a process group
 *
 * Re-emitted from the image (0x00E3F160..0x00E3F23C, 222 bytes).
 *
 * Frame (link.w A6,-0x18; A5 inherited from the caller = 0xE7BE84):
 *   (0x8,A6)  pgroup_idx  word -> D4     (0xA,A6)  signal word -> D3
 *   (0xC,A6)  param       long -> D6     (0x10,A6) check_perms byte -> D5
 *   (0x12,A6) status_ret  -> A2
 *   A6-0x12   "delivered to someone" flag; D7 "saw a zombie" flag;
 *   A6-0x8    param copy handed to DELIVER; A6-0xC DELIVER's status
 *   (never examined)
 *
 * Per allocated entry in the group (+0x10 == idx): a zombie (0x2000) only
 * sets D7.  Otherwise, with check_perms TRUE, ACL_$CHECK_FAULT_RIGHTS(
 * &PROC1_$CURRENT, &entry->level1_pid) must be TRUE, or the signal must
 * be 0x16 and the entry's session (+0x5C) equal the session of the LAST
 * ENTRY A4 WAS SET TO -- A4 is loaded together with A3 at 0x00E3F1A0 and
 * never diverges from it, so the comparison at 0x00E3F1DE is the entry
 * against itself and always succeeds (original quirk, reproduced).  A
 * failure stores permission_denied in *status_ret but the walk continues.
 *
 * Final status (0x00E3F20A..0x00E3F220): if anyone was delivered to, the
 * status is left as it stands (0 or permission_denied); else a zombie
 * gives status_$proc2_zombie, else status_$proc2_uid_not_found (also the
 * answer for index 0).  The audit call is unconditional.
 *
 * Callers: SIGNAL_PGROUP 0x00E3F29E, SIGNAL_PGROUP_OS 0x00E3F314,
 * PGROUP_DECR_LEADER_COUNT 0x00E42092 / 0x00E420AA.
 *
 * Original address: 0x00e3f160
 */

#include "proc2/proc2_internal.h"

void PROC2_$SIGNAL_PGROUP_INTERNAL(int16_t pgroup_idx, int16_t signal,
                                    uint32_t param, int8_t check_perms,
                                    status_$t *status_ret)
{
    int16_t index;               /* D2 */
    proc2_info_t *entry;         /* A3 */
    proc2_info_t *session_ref;   /* A4: same entry as A3 */
    int8_t delivered;            /* A6-0x12 */
    int8_t saw_zombie;           /* D7 */
    uint32_t param_copy;         /* A6-0x8 */
    status_$t deliver_status;    /* A6-0xC */

    /* 0x00E3F17C: tst.w D4w / beq.w 0x00E3F21C */
    if (pgroup_idx == 0) {
        *status_ret = status_$proc2_uid_not_found;           /* 0x00E3F21C */
        goto audit;
    }

    /* 0x00E3F182-0x00E3F18A */
    *status_ret = status_$ok;
    delivered = 0;
    saw_zombie = 0;
    param_copy = param;

    /* 0x00E3F18E-0x00E3F208 */
    index = (int16_t)PROC2_$UNWIRED_DATA.info_alloc_ptr;
    while (index != 0) {
        entry = P2_INFO_ENTRY(index);                        /* 0x00E3F194-0x00E3F1A4 */
        session_ref = entry;

        /* 0x00E3F1A6: entry+0x10 == pgroup_idx */
        if (entry->pgroup_table_idx == (uint16_t)pgroup_idx) {
            /* 0x00E3F1AC-0x00E3F1B4: btst #13 */
            if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
                saw_zombie = (int8_t)0xFF;                   /* 0x00E3F1B6 */
            } else {
                int8_t allowed = (int8_t)0xFF;
                /* 0x00E3F1BA: tst.b D5b / bpl -> deliver */
                if (check_perms < 0) {
                    /* 0x00E3F1BE-0x00E3F1D2: (&PROC1_$CURRENT, &entry->level1_pid) */
                    if (ACL_$CHECK_FAULT_RIGHTS(&PROC1_$CURRENT,
                                                &entry->level1_pid) >= 0) {
                        /* 0x00E3F1D4-0x00E3F1E2 */
                        if (!(signal == 0x16 &&
                              entry->session_id == session_ref->session_id)) {
                            allowed = 0;
                        }
                    }
                }
                if (allowed < 0) {
                    /* 0x00E3F1E4-0x00E3F1F8 */
                    PROC2_$DELIVER_SIGNAL_INTERNAL(index, signal, param_copy, &deliver_status);
                    delivered = (int8_t)0xFF;
                } else {
                    *status_ret = status_$proc2_permission_denied;   /* 0x00E3F1FE */
                }
            }
        }
        index = (int16_t)entry->next_index;                  /* 0x00E3F204 */
    }

    /* 0x00E3F20A-0x00E3F220 */
    if (delivered >= 0) {
        if (saw_zombie < 0) {
            *status_ret = status_$proc2_zombie;              /* 0x00E3F214 */
        } else {
            *status_ret = status_$proc2_uid_not_found;       /* 0x00E3F21C */
        }
    }

audit:
    /* 0x00E3F222-0x00E3F230: (2, idx, signal, param, *status_ret), result slot */
    PROC2_$LOG_SIGNAL_EVENT(2, pgroup_idx, (uint16_t)signal, param, *status_ret);
}
