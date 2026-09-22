/*
 * PGROUP_SET_INTERNAL - Move a process into a process group
 *
 * Re-emitted from the image (0x00E41E86..0x00E42022, 414 bytes).
 *
 * Frame (link.w A6,-0x28; A5 = 0xE7BE84):
 *   (0x8,A6)  entry      -> A2 (unbiased)
 *   (0xC,A6)  new_upgid  -> D2 (word)
 *   (0xE,A6)  status_ret -> A3
 *
 * PGROUP_TABLE fields are addressed as (0x3F30,A0) ref_count, (0x3F32)
 * leader_count, (0x3F34) upgid, (0x3F36) session_id with A0 = 0xEA551C +
 * idx*8.  Other entries are biased (base + idx*0xE4): (-0xD4) = +0x10
 * pgroup index, (-0x88) = +0x5C session, (-0xC2) = +0x22 next sibling.
 *
 * Callers: INIT_ENTRY_INTERNAL 0x00E733DA, SET_PGROUP 0x00E411DE,
 * SET_SESSION_ID 0x00E41D04.
 *
 * Original address: 0x00e41e86
 */

#include "proc2/proc2_internal.h"
#include "misc/crash_system.h"

/*
 * 0x00E42024 (`pea (0x10e,PC)` at 0x00E41F14): bytes 00 19 00 16 =
 * status_$proc2_process_using_pgroup_id, the crash status when no free
 * slot exists.
 */
static const status_$t proc2_pgroup_table_full_00e42024 = status_$proc2_process_using_pgroup_id;

void PGROUP_SET_INTERNAL(proc2_info_t *entry, uint16_t new_upgid, status_$t *status_ret)
{
    int16_t new_idx;             /* D0 */
    int16_t i;
    proc2_info_t *other;         /* A0 (biased) */
    uint16_t child_pgroup;       /* D2 (reused) */

    /* 0x00E41EA0 */
    *status_ret = status_$ok;

    /* 0x00E41EA2: tst.w D2w / bne */
    if (new_upgid == 0) {
        /* 0x00E41EA6-0x00E41EB2: PGROUP_CLEANUP_INTERNAL(entry, 2); then
         * 0x00E41F1E clr.w (0x10,A2); exit */
        PGROUP_CLEANUP_INTERNAL(entry, 2);
        entry->pgroup_table_idx = 0;
        return;
    }

    /* 0x00E41EB4-0x00E41EC0 */
    new_idx = PGROUP_FIND_BY_UPGID(new_upgid);
    if (new_idx != 0) {
        /*
         * 0x00E41EC2-0x00E41EE0: sign-extended entry->session_id vs
         * zero-extended PGROUP[new].session_id, compared as longwords.
         */
        if ((int32_t)(int16_t)entry->session_id !=
            (int32_t)(uint32_t)PGROUP_ENTRY(new_idx)->session_id) {
            *status_ret = status_$proc2_pgroup_in_different_session;   /* 0x00E41EE2 */
            return;                                          /* 0x00E41EE8 */
        }
        PGROUP_ENTRY(new_idx)->ref_count += 1;               /* 0x00E41EEC */
    } else {
        /*
         * 0x00E41EF2-0x00E41F0A: scan slots 1..70 (moveq #0x45 + dbf) for
         * ref_count == 0; D0 walks 1.. and ends at 71 when none is free.
         */
        new_idx = 1;
        for (i = 0; i < 70; i++) {
            if (PGROUP_ENTRY(new_idx)->ref_count == 0) {
                break;
            }
            new_idx++;
        }

        /* 0x00E41F0E: cmpi.w #0x46,D0w / ble -- 71 means full */
        if (new_idx > 0x46) {
            /* 0x00E41F14-0x00E41F22: crash; if it returns, clear and exit */
            CRASH_SYSTEM(&proc2_pgroup_table_full_00e42024);
            entry->pgroup_table_idx = 0;
            return;
        }

        /* 0x00E41F26-0x00E41F42: move.l #0x10000 -> ref_count 1, leader 0 */
        PGROUP_ENTRY(new_idx)->ref_count = 1;
        PGROUP_ENTRY(new_idx)->leader_count = 0;
        PGROUP_ENTRY(new_idx)->upgid = new_upgid;
        PGROUP_ENTRY(new_idx)->session_id = entry->session_id;
    }

    /* 0x00E41F48-0x00E41F5E: drop the old group's reference */
    if (entry->pgroup_table_idx != 0) {
        PGROUP_ENTRY(entry->pgroup_table_idx)->ref_count -= 1;
    }

    /* 0x00E41F62-0x00E41FB6: the parent, if in the same session */
    if (entry->parent_pgroup_idx != 0) {
        other = P2_INFO_ENTRY((int16_t)entry->parent_pgroup_idx);   /* mulu */
        if (entry->session_id == other->session_id) {        /* 0x00E41F7A-0x00E41F82 */
            /* 0x00E41F84-0x00E41F9C: old group, if any and not the
             * parent's, loses a leader */
            if (entry->pgroup_table_idx != 0 &&
                entry->pgroup_table_idx != other->pgroup_table_idx) {
                PGROUP_ENTRY(entry->pgroup_table_idx)->leader_count -= 1;
            }
            /* 0x00E41FA0-0x00E41FB6: new group, if not the parent's, gains one */
            if ((uint16_t)new_idx != other->pgroup_table_idx) {
                PGROUP_ENTRY(new_idx)->leader_count += 1;
            }
        }
    }

    /* 0x00E41FBA-0x00E42014: every child in the same session */
    i = (int16_t)entry->first_child_idx;
    while (i != 0) {
        other = P2_INFO_ENTRY(i);                            /* muls */
        if (entry->session_id == other->session_id) {        /* 0x00E41FD0-0x00E41FD8 */
            child_pgroup = other->pgroup_table_idx;          /* 0x00E41FDA */
            /* 0x00E41FDE-0x00E41FF2: child in our OLD group -> that group
             * gains a leader (we are leaving it) */
            if (child_pgroup != 0 && child_pgroup == entry->pgroup_table_idx) {
                PGROUP_ENTRY(child_pgroup)->leader_count += 1;
            }
            /* 0x00E41FF6-0x00E4200C: child in our NEW group -> it loses one */
            if ((uint16_t)new_idx == other->pgroup_table_idx) {
                PGROUP_ENTRY(other->pgroup_table_idx)->leader_count -= 1;
            }
        }
        i = (int16_t)other->next_child_sibling;              /* 0x00E42010 */
    }

    /* 0x00E42016 */
    entry->pgroup_table_idx = (uint16_t)new_idx;
}
