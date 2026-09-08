/*
 * AREA_$TRANSFER - Transfer area ownership to another address space
 *
 * Original address: 0x00E08098 .. 0x00E082A6 (528 bytes)
 */

#include "area/area_internal.h"

/*
 * AREA_$TRANSFER - Transfer area ownership to another address space
 *
 * Moves an area from the caller's address space to `new_asid`, re-bases it
 * on `new_seg_idx`, resizes it to `new_virt_size`, and re-threads its entry
 * from the old ASID's list head to the new one.  The caller must be the
 * current owner.
 *
 * Parameters (prologue 0x00E080A6-0x00E080B2):
 *   handle_ptr     A6+0x08 long - the caller's area handle; +0x02 is the id
 *   new_asid       A6+0x0C word - the receiving address space (D2)
 *   new_seg_idx    A6+0x0E word - the area's new first segment index (D3)
 *   new_virt_size  A6+0x10 long - the area's new virtual size (D4)
 *   status_ret     A6+0x14 long - output status
 *
 * Returns (0x00E0829C `move.w D2w,D0w`): D2 enters the routine holding
 * `new_asid` and is overwritten with the area's OLD first_seg_index exactly
 * once, at 0x00E08274, on the fully successful path.  So a success returns
 * the old first segment index and EVERY failure returns `new_asid`
 * unchanged.  (source-zm73: the tree had these two the wrong way round.)
 *
 * Original address: 0x00E08098
 */
int16_t AREA_$TRANSFER(area_$handle_t *handle_ptr, int16_t new_asid,
                       int16_t new_seg_idx, uint32_t new_virt_size,
                       status_$t *status_ret)
{
    uint16_t area_id;
    area_$entry_t *entry;
    status_$t status;
    int16_t old_seg_idx;
    int16_t seg_adjustment;
    uint32_t old_virt_size;

    /*
     * D2.  It is the `new_asid` argument for the whole body and only becomes
     * the return value at 0x00E08274.
     */
    int16_t result = new_asid;

    /* 0x00E080B6-0x00E080C0 */
    area_id = AREA_HANDLE_TO_ID(*handle_ptr);
    if (area_id == 0 || area_id > (uint16_t)AREA_$N_AREAS) {
        /*
         * 0x00E080C2-0x00E080CC.  This path writes the CALLER's cell
         * directly and jumps past the `move.l (-0x4,A6),(A0)` at the tail,
         * so the frame's status local is never involved.
         */
        *status_ret = status_$area_not_active;
        return result;
    }

    /* 0x00E080D0-0x00E080E2 */
    entry = AREA_ID_TO_ENTRY(area_id);

    /* 0x00E080E6: the frame-local status at A6-0x04 */
    status = status_$ok;

    /* 0x00E080EA */
    ML_$LOCK(ML_LOCK_AREA);

    /* 0x00E080F8-0x00E08108 */
    while ((entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }

    /* 0x00E0810A-0x00E08118 */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0) {
        status = status_$area_not_active;
        goto unlock_and_return;
    }

    /* 0x00E0811C-0x00E08130 */
    if (entry->owner_asid != PROC1_$AS_ID) {
        status = status_$area_not_owner;
        goto unlock_and_return;
    }

    /* 0x00E08134-0x00E08146 */
    entry->flags |= AREA_FLAG_IN_TRANS;
    ML_$UNLOCK(ML_LOCK_AREA);

    /* 0x00E08148: D5 */
    old_virt_size = entry->virt_size;

    /* 0x00E0814C: `bcc` - unsigned, so this is the shrink case */
    if (new_virt_size < old_virt_size) {
        /* 0x00E08150-0x00E08168 */
        area_$resize((int16_t)area_id, entry, new_virt_size,
                     entry->commit_size, 1, &status);

        /* 0x00E0816C-0x00E08170 */
        if (status != status_$ok) {
            goto lock_and_clear;
        }
    }

    /* 0x00E08174 */
    ML_$LOCK(ML_LOCK_PMAP);

    /*
     * 0x00E08182-0x00E08194: computed unconditionally, before the reversed
     * test that consumes it.
     */
    seg_adjustment = (int16_t)(((entry->virt_size + 0x7FFF) >> 15) - 1);

    /* 0x00E0818E: D6 - the value this routine returns on success */
    old_seg_idx = entry->first_seg_index;

    /* 0x00E08196-0x00E0819A */
    entry->first_bste = new_asid;
    entry->first_seg_index = new_seg_idx;

    /* 0x00E0819E-0x00E081AC */
    if ((entry->flags & AREA_FLAG_REVERSED) != 0) {
        entry->first_seg_index = (int16_t)(entry->first_seg_index
                                           + seg_adjustment);
    }

    /* 0x00E081B0 */
    ML_$UNLOCK(ML_LOCK_PMAP);

    /* 0x00E081BE: `bls` - unsigned, so this is the grow case */
    if (new_virt_size > old_virt_size) {
        /* 0x00E081C2-0x00E081DA */
        area_$resize((int16_t)area_id, entry, new_virt_size,
                     entry->commit_size, 1, &status);

        /* 0x00E081DE */
        if (status != status_$ok) {
            /* 0x00E081E4-0x00E0820C: put the old owner and index back */
            ML_$LOCK(ML_LOCK_PMAP);
            entry->first_bste = PROC1_$AS_ID;
            entry->first_seg_index = old_seg_idx;
            ML_$UNLOCK(ML_LOCK_PMAP);

            goto lock_and_clear;
        }
    }

    /* 0x00E0821E */
    ML_$LOCK(ML_LOCK_AREA);

    /*
     * 0x00E0822C-0x00E08252: unlink from the old ASID's list.  The `next`
     * test comes first, then the `prev` test, and only a null `prev` reaches
     * the list head.
     */
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    }
    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    } else {
        /*
         * 0x00E08246-0x00E08250: `lsl.w #0x2` on the entry's own owner_asid,
         * indexing AREA_$GLOBALS.asid_list at globals+0x4D8.
         */
        AREA_$ASID_LIST[entry->owner_asid] = entry->next;
    }

    /* 0x00E08254-0x00E0826C: push onto the new ASID's list */
    entry->next = AREA_$ASID_LIST[new_asid];
    if (entry->next != NULL) {
        entry->next->prev = entry;
    }
    entry->prev = NULL;
    AREA_$ASID_LIST[new_asid] = entry;

    /* 0x00E08270 */
    entry->owner_asid = new_asid;

    /* 0x00E08274: the ONLY write to D2 - the success return value */
    result = old_seg_idx;

    goto clear_in_trans;

lock_and_clear:
    /*
     * 0x00E0820E-0x00E0821C.  Both resize failures reach the tail through
     * this three-instruction stub, which re-takes lock 0x0E (dropped at
     * 0x00E08146) and falls into 0x00E08276.
     */
    ML_$LOCK(ML_LOCK_AREA);

clear_in_trans:
    /* 0x00E08276-0x00E08286 */
    entry->flags &= (uint16_t)~AREA_FLAG_IN_TRANS;
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

unlock_and_return:
    /* 0x00E08288-0x00E08298 */
    ML_$UNLOCK(ML_LOCK_AREA);
    *status_ret = status;

    /* 0x00E0829C */
    return result;
}
