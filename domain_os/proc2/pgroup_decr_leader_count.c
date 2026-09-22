/*
 * PGROUP_DECR_LEADER_COUNT - Drop one leader from a process group
 *
 * Re-emitted from the image (0x00E42028..0x00E420B6, 144 bytes).
 *
 * A nested Pascal procedure of PGROUP_CLEANUP_INTERNAL: 0x00E42034
 * `movea.l (A6),A2` picks up the static link (the enclosing frame) and the
 * status handed to PROC2_$SIGNAL_PGROUP_INTERNAL is `pea (-0x8,A2)`, the
 * enclosing procedure's A6-0x8 scratch.  PGROUP_CLEANUP_INTERNAL never
 * reads that slot (nothing in 0x00E420B8..0x00E4216C references -0x8), so
 * a local of this routine stands in for it here; A5 is inherited from the
 * caller (0xE7BE84) rather than reloaded.
 *
 * Frame (link.w A6,-0x14): (0x8,A6) pgroup_idx word -> D2.  The zero test
 * at 0x00E42036 is on that move.w (movea sets no flags).
 *
 * When the group's leader count (PGROUP_TABLE[idx].leader_count, at
 * (0x3F32,A0) with A0 = 0xEA551C + idx*8) reaches zero and some allocated
 * entry in the group has flags bit 0x0040 set (btst.b #6 on the low byte),
 * the whole group is sent SIGHUP (1) and then SIGCONT (0x16 = 22 in this
 * kernel's numbering; see below), both with param 0 and check_perms FALSE.
 *
 * Callers: PGROUP_CLEANUP_INTERNAL 0x00E42118 / 0x00E4214C.
 *
 * Original address: 0x00e42028
 */

#include "proc2/proc2_internal.h"

/*
 * 0x00E420A4: `move.w #0x16,-(SP)` -- the second signal is 22, which
 * proc2.h names SIGTTOU, not SIGCONT (19).  Spelled numerically so the
 * image value is what gets pushed.  TODO: confirm the SR10.2 signal
 * numbering for 22 (bead source-t3cp).
 */
#define PGROUP_ORPHAN_SIGNAL_1   1      /* 0x00E4208C: SIGHUP */
#define PGROUP_ORPHAN_SIGNAL_2   0x16   /* 0x00E420A4 */

void PGROUP_DECR_LEADER_COUNT(int16_t pgroup_idx)
{
    int16_t index;               /* D1 */
    int8_t has_leader;           /* D0b */
    proc2_info_t *entry;         /* A0 (biased) */
    status_$t status;            /* the enclosing frame's A6-0x8 */

    /* 0x00E42036: beq exit on pgroup_idx == 0 */
    if (pgroup_idx == 0) {
        return;
    }

    /* 0x00E42038-0x00E4204C: leader_count -= 1; bne exit */
    PGROUP_ENTRY(pgroup_idx)->leader_count -= 1;
    if (PGROUP_ENTRY(pgroup_idx)->leader_count != 0) {
        return;
    }

    /* 0x00E4204E-0x00E4207C: walk the allocated list; no early exit */
    has_leader = 0;
    index = (int16_t)P2_INFO_ALLOC_PTR;
    while (index != 0) {
        entry = P2_INFO_ENTRY(index);
        /* 0x00E42068: btst.b #0x6,(-0xb9,A0) -> flags & 0x0040;
         * 0x00E42070: entry+0x10 == pgroup_idx */
        if ((entry->flags & 0x0040) != 0 &&
            entry->pgroup_table_idx == (uint16_t)pgroup_idx) {
            has_leader = (int8_t)0xFF;                       /* 0x00E42076: st */
        }
        index = (int16_t)entry->next_index;                  /* 0x00E42078 */
    }

    /* 0x00E4207E: tst.b D0b / bpl exit */
    if (has_leader < 0) {
        /* 0x00E42082-0x00E42096: (idx, 1, 0L, FALSE, &status), result slot */
        PROC2_$SIGNAL_PGROUP_INTERNAL(pgroup_idx, PGROUP_ORPHAN_SIGNAL_1, 0, 0, &status);
        /* 0x00E4209A-0x00E420AA: (idx, 0x16, 0L, FALSE, &status) */
        PROC2_$SIGNAL_PGROUP_INTERNAL(pgroup_idx, PGROUP_ORPHAN_SIGNAL_2, 0, 0, &status);
    }
}
