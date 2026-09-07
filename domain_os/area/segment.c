/*
 * AREA_$THREAD_BSTES - Thread BSTE entries for area
 * AREA_$REMOVE_SEG - Remove segment from area
 * AREA_$DEACTIVATE_ASTE - Deactivate AST entry for area
 *
 * Original addresses:
 *   AREA_$THREAD_BSTES: 0x00E09722
 *   AREA_$REMOVE_SEG: 0x00E09822
 *   AREA_$DEACTIVATE_ASTE: 0x00E09EF4
 */

#include "area/area_internal.h"

/*
 * AREA_$THREAD_BSTES - Thread BSTE entries for area
 *
 * Links BSTE (Backing Store Table Entry) entries for the area.
 * This sets up the initial BSTE index and segment index for the area.
 *
 * Parameters:
 *   handle_ptr  - Pointer to area handle
 *   bste_idx    - First BSTE index
 *   seg_idx     - First segment index
 *   param_4     - Unknown parameter
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E09722
 */
void AREA_$THREAD_BSTES(area_$handle_t *handle_ptr, int16_t bste_idx,
                        int16_t seg_idx, uint32_t param_4,
                        status_$t *status_ret)
{
    uint16_t area_id = AREA_HANDLE_TO_ID(*handle_ptr);
    int16_t generation = AREA_HANDLE_TO_GEN(*handle_ptr);
    area_$entry_t *entry;
    int entry_offset;

    (void)param_4;  /* Unused parameter */

    /* Validate area ID */
    if (area_id == 0 || area_id > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return;
    }

    /* Calculate entry offset and get entry pointer */
    entry_offset = (uint32_t)area_id * AREA_ENTRY_SIZE;
    entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

    ML_$LOCK(ML_LOCK_AREA);

    /* Wait if area is in transition */
    while ((entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }

    /* Validate area is active and generation matches */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0 ||
        entry->generation != generation) {
        *status_ret = status_$area_not_active;
        ML_$UNLOCK(ML_LOCK_AREA);
        return;
    }

    /* Mark area as in-transition */
    entry->flags |= AREA_FLAG_IN_TRANS;

    ML_$UNLOCK(ML_LOCK_AREA);

    *status_ret = status_$ok;

    /* If first BSTE not yet set, initialize it */
    if (entry->first_bste == -1) {
        entry->first_bste = bste_idx;

        /* For reversed areas, adjust segment index */
        if ((entry->flags & AREA_FLAG_REVERSED) != 0) {
            int seg_count = (entry->virt_size + 0x7FFF) >> 15;
            seg_idx += seg_count - 1;
        }

        entry->first_seg_index = seg_idx;
    }

    ML_$LOCK(ML_LOCK_AREA);

    /* Clear in-transition flag */
    entry->flags &= ~AREA_FLAG_IN_TRANS;

    /* Advance in-transition event count */
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

    ML_$UNLOCK(ML_LOCK_AREA);
}

/*
 * AREA_$REMOVE_SEG - Remove a segment from an area
 *
 * Original address: 0x00E09822, 584 bytes.
 *
 * Recovered argument shape (from the prologue at 0x00E09830-0x00E09848 and
 * the sole call site at 0x00E449F8-0x00E44A10):
 *
 *   (0x08,A6) long  seg_rec     `pea (-0x3fc,A2)` -- a record whose +0x00
 *                               is a word compared against the area entry's
 *                               +0x2C, and whose +0x02 is the area id
 *   (0x0C,A6) word  arg_0c      compared against the area entry's +0x26
 *   (0x0E,A6) word  arg_0e      added into the segment-count comparison
 *   (0x10,A6) byte  arg_10      boolean, ANDed into the "delete the area"
 *                               decision (`and.b D5b,D0b` at 0x00E09932)
 *   (0x12,A6) word  arg_12      segment index / count, negated on the
 *                               "grows down" path at 0x00E098CA
 *   (0x14,A6) long  status_ret
 *
 * TODO(source-u4b): AREA_$REMOVE_SEG (0x00E09822, 584 bytes) is not yet
 * decompiled.  The body below reproduces only the `clr.l (A0)` at
 * 0x00E09848.  Missing: the area-id range check against AREA_$N_AREAS at
 * (0x5E2,A5) with status 0x00320006, the area table walk at
 * 0xD94C00 + area_id*0x30 - 0x30, the ML_$LOCK(0x0E) /
 * area_$wait_in_trans (0x00E07742) in-transition spin on bit 4 of the
 * entry's +0x2E flag word, the two "is this the whole area" tests
 * (0x00E098C0-0x00E09932) that decide between area_$internal_delete
 * (0x00E07B50) and a partial removal, the 32-entry physical-map scan at
 * 0xED5000 + seg*0x80 that collects PTEs for MMU_$REMOVE_LIST
 * (0x00E23D92), the AREA_$FIND_ENTRY_BY_UID call (0x00E0939C) and the
 * EC_$ADVANCE on AREA_$IN_TRANS_EC.  Tracked by bead source-u4b
 * ("Complete area/segment subsystem").
 */
void AREA_$REMOVE_SEG(void *seg_rec, uint16_t arg_0c, uint16_t arg_0e,
                      int8_t arg_10, uint16_t arg_12,
                      status_$t *status_ret)
{
    (void)seg_rec;
    (void)arg_0c;
    (void)arg_0e;
    (void)arg_10;
    (void)arg_12;

    /* 0x00E09848: clr.l (A0) */
    *status_ret = status_$ok;
}

/*
 * AREA_$DEACTIVATE_ASTE - Deactivate the AST entry backing an area segment
 *
 * Original address: 0x00E09EF4, 924 bytes.
 *
 * Argument shape (prologue 0x00E09F02-0x00E09F0E):
 *   (0x08,A6) long  aste        aste->aote is read at (0x4,A1)
 *   (0x0C,A6) long  status_ret
 *
 * TODO(source-u4b): AREA_$DEACTIVATE_ASTE (0x00E09EF4, 924 bytes) is not
 * yet decompiled.  The body below reproduces only the `clr.l (A0)` at
 * 0x00E09F08.  Missing: the whole function under ML_$LOCK(0x12) -- the
 * two segment-table lookups (the inline `(0x18,A2,seg*4)` form for
 * segment index < 2 and the chained lookup through (0x68,A5) plus
 * M$OIU$WLW for larger ones, with the CRASH_SYSTEM at 0x00E09F74 when the
 * chain runs out), the ASTE flag-0x2000 gate, the 32-longword physical map
 * copy from 0xED5000 + seg*0x80, the BSTE allocate/lookup calls at
 * 0x00E3B0D6 / 0x00E3A5B0 / 0x00E3A8B6, the MMAPE chain fix-ups at
 * 0xEC5400 + n*0x14, the busy-bit protocol on bit 6 of the segment-table
 * entry's byte +0x01 (with status 0x00030004 when it is already set), the
 * EC_$ADVANCE on (0x58,A5), and the final unlink of the ASTE from its
 * chain with the CRASH_SYSTEM at 0x00E0A260.  Tracked by bead source-u4b
 * ("Complete area/segment subsystem").
 */
void AREA_$DEACTIVATE_ASTE(void *aste, status_$t *status_ret)
{
    (void)aste;

    /* 0x00E09F08: clr.l (A0) */
    *status_ret = status_$ok;
}
