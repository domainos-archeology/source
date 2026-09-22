/*
 * PROC2_$PGROUP_INFO - Report a process group's session and leader state
 *
 * Re-emitted from the image (0x00E41DBC..0x00E41E84, 202 bytes) and
 * verified block by block; the previous text was faithful.
 *
 * Frame (link.w A6,-0x14; A5 = 0xE7BE84):
 *   (0x8,A6)  pgroup_id ptr -> A0     (0xC,A6)  session_id_ret -> A2
 *   (0x10,A6) is_leader_ret -> A3     (0x14,A6) status_ret -> D4
 *   A6-0x4 status
 *
 * A zero UPGID answers (0, TRUE, status_$ok) without locking.  Otherwise
 * the slot is found by UPGID, or failing that through the allocated
 * entry whose upid equals it; is_leader is `seq` on leader_count == 0.
 *
 * Only reference: the SVC table entry at 0x00E7BAEE.
 *
 * Original address: 0x00e41dbc
 */

#include "proc2/proc2_internal.h"

void PROC2_$PGROUP_INFO(uint16_t *pgroup_id, uint16_t *session_id_ret,
                        uint8_t *is_leader_ret, status_$t *status_ret)
{
    uint16_t upgid;              /* D2 */
    int16_t pgroup_idx;          /* D0 */
    int16_t index;               /* D1 */
    proc2_info_t *entry;
    pgroup_entry_t *pgroup;
    status_$t status;            /* A6-0x4 */
    uint16_t session_id;         /* D3 */
    uint8_t is_leader;           /* D2b */

    /* 0x00E41DDA-0x00E41DE6 */
    if (*pgroup_id == 0) {
        *session_id_ret = 0;
        *is_leader_ret = 0xFF;                               /* 0x00E41DE0: st */
        *status_ret = status_$ok;
        return;
    }

    /* 0x00E41DEA-0x00E41DF8 */
    upgid = *pgroup_id;
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E41DFA-0x00E41E06 */
    pgroup_idx = PGROUP_FIND_BY_UPGID(upgid);
    if (pgroup_idx == 0) {
        /* 0x00E41E08-0x00E41E30: first allocated entry whose upid matches */
        index = (int16_t)P2_INFO_ALLOC_PTR;
        while (index != 0) {
            entry = P2_INFO_ENTRY(index);
            if (upgid == entry->upid) {                      /* 0x00E41E20 */
                pgroup_idx = (int16_t)entry->pgroup_table_idx;   /* 0x00E41E26 */
                break;
            }
            index = (int16_t)entry->next_index;              /* 0x00E41E2C */
        }
    }

    /* 0x00E41E32/0x00E41E34: tst.w D0w / beq -> not found */
    if (pgroup_idx == 0) {
        status = status_$proc2_uid_not_found;                /* 0x00E41E56 */
    } else {
        /* 0x00E41E36-0x00E41E50 */
        pgroup = PGROUP_ENTRY(pgroup_idx);
        session_id = pgroup->session_id;                     /* (0x3F36) */
        is_leader = (pgroup->leader_count == 0) ? 0xFF : 0;  /* seq on (0x3F32) */
        status = status_$ok;
    }

    /* 0x00E41E5E-0x00E41E6A */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E41E6C-0x00E41E78 */
    if (status == status_$ok) {
        *session_id_ret = session_id;
        *is_leader_ret = is_leader;
    }
    *status_ret = status;
}
