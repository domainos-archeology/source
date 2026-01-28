/*
 * PGROUP_DECR_LEADER_COUNT - Decrement process group leader count
 *
 * Internal helper that decrements the leader count for a process group.
 * If the leader count reaches zero, signals the pgroup with SIGHUP and SIGCONT
 * to notify orphaned processes.
 *
 * This is called when a session leader exits or changes process groups,
 * potentially orphaning the remaining processes in the group.
 *
 * Parameters:
 *   pgroup_idx - Index into the pgroup table
 *
 * Original address: 0x00e42028
 */

#include "proc2/proc2_internal.h"

/*
 * Flag at offset 0x2B in proc2_info_t (low byte of flags):
 * Bit 6 (0x40) indicates this process is a session leader
 */
#define PROC2_FLAG_SESSION_LEADER_BYTE  0x40

/*
 * Memory access helpers for iteration.
 * These access fields from proc2_info_t using index-based addressing.
 */
#if defined(ARCH_M68K)
    /* Base: 0xEA5438 is P2_INFO_TABLE_BASE, field addresses are base + offset + idx*0xE4 */
    #define P2_PGROUP_IDX_FIELD(idx)    (*(int16_t*)(0xEA5448 + (idx) * 0xE4))   /* offset 0x10 */
    #define P2_NEXT_ALLOC_IDX(idx)      (*(int16_t*)(0xEA544A + (idx) * 0xE4))   /* offset 0x12 */
    #define P2_FLAGS_BYTE_LOW(idx)      (*(uint8_t*)(0xEA5463 + (idx) * 0xE4))   /* offset 0x2B */
#else
    #define P2_PGROUP_IDX_FIELD(idx)    (P2_INFO_ENTRY(idx)->pgroup_table_idx)
    #define P2_NEXT_ALLOC_IDX(idx)      (P2_INFO_ENTRY(idx)->next_index)
    #define P2_FLAGS_BYTE_LOW(idx)      ((uint8_t)(P2_INFO_ENTRY(idx)->flags & 0xFF))
#endif

void PGROUP_DECR_LEADER_COUNT(int16_t pgroup_idx)
{
    pgroup_entry_t *pgroup;
    int16_t cur_idx;
    int8_t has_session_leader;
    status_$t status;

    /* Nothing to do for pgroup 0 */
    if (pgroup_idx == 0) {
        return;
    }

    /* Decrement the leader count */
    pgroup = PGROUP_ENTRY(pgroup_idx);
    pgroup->leader_count--;

    /* If leader count is still > 0, nothing more to do */
    if (pgroup->leader_count != 0) {
        return;
    }

    /*
     * Leader count reached zero. Check if any process in this group
     * is a session leader. If so, the group is orphaned and needs to
     * be notified with SIGHUP and SIGCONT.
     */
    has_session_leader = 0;
    cur_idx = P2_INFO_ALLOC_PTR;

    while (cur_idx != 0) {
        /* Check if this process is in the target pgroup and is a session leader */
        if ((P2_FLAGS_BYTE_LOW(cur_idx) & PROC2_FLAG_SESSION_LEADER_BYTE) != 0) {
            if (pgroup_idx == P2_PGROUP_IDX_FIELD(cur_idx)) {
                has_session_leader = -1;
                break;
            }
        }

        cur_idx = P2_NEXT_ALLOC_IDX(cur_idx);
    }

    /* If the group has a session leader, signal the orphaned group */
    if (has_session_leader < 0) {
        /* Send SIGHUP to the process group */
        PROC2_$SIGNAL_PGROUP_INTERNAL(pgroup_idx, SIGHUP, 0, 0, &status);

        /* Send SIGCONT to continue any stopped processes */
        PROC2_$SIGNAL_PGROUP_INTERNAL(pgroup_idx, SIGCONT, 0, 0, &status);
    }
}
