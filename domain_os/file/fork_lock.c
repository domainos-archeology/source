/*
 * FILE_$FORK_LOCK - hand the parent's file locks to the child during fork
 *
 * Original address: 0x00E74244, 204 bytes.
 * Module base A5 = 0x00E8605C (`lea (0xe8605c).l,A5` at 0x00E7424C); this
 * function never actually uses A5.
 *
 * The child inherits every per-process lock slot the parent holds, and each
 * shared lock entry's reference count goes up by one.
 *
 * Address arithmetic in the listing:
 *   per-process slot   0x00EA202C + ASID*0x12C + slot*2 - 0x2662
 *                    = 0x00E9F9CC + ASID*0x12C + (slot-1)*2
 *                    = FILE_$PROC_LOT_SLOT(ASID, slot)
 *   per-process count  0x00EA202C + ASID*2 + 0x1D98
 *                    = 0x00EA3DC4 + ASID*2
 *                    = FILE_$PROC_LOT_COUNT(ASID)
 *   refcount           0x00E935CC + entry*0x1C - 4
 *                    = FILE_$LOT_ENTRY(entry)->refcount
 */

#include "file/file_internal.h"
#include "ml/ml.h"

/*
 * FILE_$FORK_LOCK - duplicate the parent's lock slots into the child
 *
 * Parameters:
 *   new_asid   A6+0x08  pointer to the child's ASID (Pascal var parameter;
 *                       re-read on every loop iteration at 0x00E742C2)
 *   status_ret A6+0x0C  out: always cleared to status_$ok (0x00E74260)
 */
void FILE_$FORK_LOCK(uint16_t *new_asid, status_$t *status_ret)
{
    int16_t parent_asid;
    int16_t slot_count;
    int16_t i;

    parent_asid = (int16_t)PROC1_$AS_ID;        /* 0x00E7425A */
    *status_ret = status_$ok;                   /* 0x00E74260 */

    ML_$LOCK(FILE_LOT_ML_LOCK_ID);              /* 0x00E74262 */

    slot_count = (int16_t)FILE_$PROC_LOT_COUNT(parent_asid);   /* 0x00E74280 */

    if (slot_count != 0) {
        /* 0x00E7428C: subq.w #1 + dbf => slot_count iterations, slots 1..N. */
        for (i = 1; i <= slot_count; i++) {
            uint16_t entry_idx = FILE_$PROC_LOT_SLOT(parent_asid, i);

            if (entry_idx == 0) {
                continue;                       /* 0x00E742B0 */
            }
            /* 0x00E742C2: the child ASID is fetched through the var
             * parameter on every iteration, not hoisted. */
            FILE_$PROC_LOT_SLOT((int16_t)*new_asid, i) = entry_idx;
            FILE_$LOT_ENTRY(entry_idx)->refcount++;             /* 0x00E742D8 */
        }
    }

    /* 0x00E742E2 */
    FILE_$PROC_LOT_COUNT((int16_t)*new_asid) =
        FILE_$PROC_LOT_COUNT(parent_asid);

    ML_$UNLOCK(FILE_LOT_ML_LOCK_ID);            /* 0x00E742FA */
}
