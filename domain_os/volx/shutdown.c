/*
 * VOLX_$SHUTDOWN - Dismount all volumes
 *
 * Iterates through all mounted volumes and dismounts them.
 * Returns the first error encountered (but continues with remaining volumes).
 *
 * Original address: 0x00E6B508
 */

#include "volx/volx_internal.h"

/*
 * VOLX_$SHUTDOWN
 *
 * Returns:
 *   First error status encountered, or status_$ok if all succeeded
 *
 * Algorithm:
 *   1. Iterate through all 6 possible volume entries
 *   2. For each entry with lv_num != 0 (i.e., mounted):
 *      a. If parent_uid is not nil, call DIR_$DROP_MOUNT
 *      b. Call AST_$DISMOUNT with force flag 0
 *      c. If successful, clear the lv_num field
 *   3. Return first error encountered
 *
 * Assembly notes (0x00E6B508):
 *   - 0x00E6B510  lea (0xe82604).l,A5    - A5 = entry 1
 *   - 0x00E6B51A  moveq #0x5,D2          - counter: 6 iterations
 *   - 0x00E6B51C  moveq #0x1,D3          - index starts at 1
 *   - 0x00E6B524  lea (0x20,A5),A3       - biased pointer, A5 + idx * 0x20
 *   - 0x00E6B5AA  lea (0x20,A3),A3       - advance one entry
 *   - fields off the biased pointer: lv_num (-0x2), parent_uid (-0x10),
 *     dir_uid (-0x20)
 *
 * Notes:
 *   - Continues with remaining volumes even if one fails
 *   - Only returns the first error status
 *   - Does not call DISK_$DISMOUNT or VTOC_$DISMOUNT
 *     (those are handled by AST_$DISMOUNT)
 */
status_$t VOLX_$SHUTDOWN(void)
{
    int16_t count;
    int16_t vol_idx;
    volx_$entry_t *entry;
    status_$t overall_status;
    status_$t local_status;
    status_$t drop_status;

    overall_status = status_$ok;
    count = 5;          /* Loop counter (5 downto -1 = 6 iterations) */
    vol_idx = 1;        /* Volume index (1-6) */
    entry = VOLX_$ENTRY(1);  /* Start at entry 1 */

    do {
        /* Check if entry is in use (lv_num != 0) */
        if (entry->lv_num != 0) {
            local_status = status_$ok;

            /* Remove mount point if parent_uid is set */
            if (entry->parent_uid.high != UID_$NIL.high ||
                entry->parent_uid.low != UID_$NIL.low) {
                /*
                 * 0x00E6B548 `pea (-0x46,PC)` -> 0x00E6B504, the same zero
                 * longword VOLX_$DISMOUNT passes (volx_internal.h).
                 */
                DIR_$DROP_MOUNT(&entry->parent_uid, &entry->dir_uid,
                                &volx_$drop_mount_lv, &drop_status);

                if (local_status == status_$ok) {
                    local_status = drop_status;
                }

                if (drop_status == status_$ok) {
                    /* Clear parent_uid */
                    entry->parent_uid = UID_$NIL;
                }
            }

            /* Call AST_$DISMOUNT with force flag 0 */
            AST_$DISMOUNT(vol_idx, 0, &drop_status);

            if (local_status == status_$ok) {
                local_status = drop_status;
                if (drop_status == status_$ok) {
                    /* Clear lv_num to mark entry as unused */
                    entry->lv_num = 0;
                }
            }

            /* Record first error */
            if (overall_status == status_$ok) {
                overall_status = local_status;
            }
        }

        vol_idx++;
        entry++;
        count--;
    } while (count >= 0);

    return overall_status;
}
