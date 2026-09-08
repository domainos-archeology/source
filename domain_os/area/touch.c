/*
 * AREA_$TOUCH - Touch area pages (bring them into memory)
 * AREA_$ASSOC - Associate one area page with a physical page
 *
 * Original addresses:
 *   AREA_$TOUCH: 0x00E094FE .. 0x00E096A0 (420 bytes)
 *   AREA_$ASSOC: 0x00E096A2 .. 0x00E09720 (128 bytes)
 */

#include "area/area_internal.h"

/*
 * AREA_$TOUCH - Touch area pages (bring them into memory)
 *
 * Locates the ASTE that backs one page of an area, grows the area's
 * committed size if the page lies past the current end, and hands the page
 * to AST_$TOUCH_AREA.
 *
 * Parameters (prologue 0x00E0950C-0x00E09514 plus the by-reference use of
 * A6+0x0C at 0x00E095A6):
 *   handle_ptr  A6+0x08 long - the caller's area handle; +0x00 is the
 *               generation word and +0x02 the area id
 *   bste_idx    A6+0x0C word - the area-relative block index.  It is passed
 *               BY ADDRESS to area_$find_entry_by_uid, which rewrites it for
 *               a reversed area, and re-read at 0x00E095BA afterwards
 *   seg_idx     A6+0x0E word - the page within that block (D3)
 *   param_4     A6+0x10 word - never read by this routine
 *   ppn_array   A6+0x12 long - handed straight to AST_$TOUCH_AREA
 *               (0x00E0965A)
 *   status_p    A6+0x16 long - output status (D5)
 *
 * NOTE: 0x14 is spelled ML_LOCK_PMAP below (ml/ml.h); ML_LOCK_AST is 0x12,
 * which is the constant AREA_$COPY uses.  area/area.h's module header comment
 * still says the opposite - TODO, bead source-5prc.
 *
 * NOTE: on EVERY path this routine returns holding ML lock 0x14 - the
 * success path takes it at 0x00E09648 before calling AST_$TOUCH_AREA and
 * keeps it, and all four failure paths funnel through the tail at
 * 0x00E0968C, whose only job is to take that same lock before the epilogue.
 * The caller is what releases it.
 *
 * Original address: 0x00E094FE
 */
void AREA_$TOUCH(area_$handle_t *handle_ptr, uint16_t bste_idx,
                 uint16_t seg_idx, int16_t param_4, uint32_t *ppn_array,
                 status_$t *status_p)
{
    uint16_t area_id;
    int16_t generation;
    area_$entry_t *entry;
    struct aste_t *aste_ptr;
    uint32_t current_pages;
    int32_t needed_pages;
    uint32_t target_size;

    (void)param_4;

    /* 0x00E09518: the id is the LOW word of the handle */
    area_id = AREA_HANDLE_TO_ID(*handle_ptr);
    /* 0x00E09570 `cmp.w (A4),D1w`: the generation is the HIGH word */
    generation = (int16_t)AREA_HANDLE_TO_GEN(*handle_ptr);

    /* 0x00E0951C-0x00E09522 */
    if (area_id == 0 || area_id > (uint16_t)AREA_$N_AREAS) {
        /* 0x00E09526, then the ML_$LOCK tail */
        *status_p = status_$area_not_active;
        goto lock_and_return;
    }

    /* 0x00E09530-0x00E09544 */
    entry = AREA_ID_TO_ENTRY(area_id);

    /* 0x00E09548 */
    ML_$LOCK(ML_LOCK_AREA);

    /* 0x00E09554-0x00E09564 */
    while ((entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }

    /*
     * 0x00E09566-0x00E09578: active, and either the generation matches or
     * the area has a remote UID.
     */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0 ||
        (entry->generation != generation && entry->remote_uid == 0)) {
        /* 0x00E0957A-0x00E09590 */
        ML_$UNLOCK(ML_LOCK_AREA);
        *status_p = status_$area_not_active;
        goto lock_and_return;
    }

    /* 0x00E09594 */
    ML_$UNLOCK(ML_LOCK_AREA);

    /* 0x00E095A2-0x00E095B6 */
    aste_ptr = area_$find_entry_by_uid((int16_t)area_id, &bste_idx,
                                       (int16_t)seg_idx, status_p);

    /* 0x00E095BA: bste_idx is re-read here, after find_entry_by_uid */

    /* 0x00E095BE-0x00E095C0 */
    if (*status_p != status_$ok) {
        goto lock_and_return;
    }

    /* 0x00E095CE/0x00E095F0: the committed size in 1K pages */
    current_pages = entry->commit_size >> 10;

    if ((entry->flags & AREA_FLAG_REVERSED) != 0) {
        /* 0x00E095CE-0x00E095EC */
        needed_pages = (int32_t)(((uint32_t)bste_idx << 5) + 0x1F
                                 - (uint32_t)seg_idx - current_pages + 1);
    } else {
        /* 0x00E095F0-0x00E09608 */
        needed_pages = (int32_t)((((uint32_t)bste_idx << 5) + (uint32_t)seg_idx)
                                 - current_pages + 1);
    }

    /* 0x00E0960A: `ble` - nothing to grow */
    if (needed_pages > 0) {
        /* 0x00E0960C-0x00E09612: at least four pages at a time */
        int32_t grow_pages = (4 >= needed_pages) ? 4 : needed_pages;

        /* 0x00E0961A-0x00E0962C: clamped to the area's virtual size */
        target_size = entry->commit_size + ((uint32_t)grow_pages << 10);
        if (target_size > entry->virt_size) {
            target_size = entry->virt_size;
        }

        /* 0x00E09614-0x00E0963E */
        area_$resize((int16_t)area_id, entry, entry->virt_size, target_size,
                     1, status_p);

        /* 0x00E09642-0x00E09646 */
        if (*status_p != status_$ok) {
            /* 0x00E09688: drop the reference, then take the lock */
            aste_ptr->wire_count--;
            goto lock_and_return;
        }
    }

    /* 0x00E09648 */
    ML_$LOCK(ML_LOCK_PMAP);

    /*
     * 0x00E09656-0x00E09676 pushes, right to left:
     *   (0x2,A4)  area_id   - the id half of the caller's area handle
     *   (0xe,A2)  the ASTE's own seg_index, BY VALUE (a word, not the ASTE)
     *   D3w       seg_idx   - the page within that block
     *   D0        (bste_idx << 5) + seg_idx, the area-relative page number
     *   (0x12,A6) ppn_array
     *   D5        status_p
     */
    AST_$TOUCH_AREA(area_id, aste_ptr->seg_index, (int16_t)seg_idx,
                    ((uint32_t)bste_idx << 5) + (uint32_t)seg_idx,
                    ppn_array, status_p);

    /* 0x00E0967C */
    entry->flags |= AREA_FLAG_TOUCHED;

    /* 0x00E09682 */
    aste_ptr->wire_count--;

    /* 0x00E09686: the success path already holds lock 0x14 */
    return;

lock_and_return:
    /*
     * 0x00E0968C-0x00E09692.  The lock is taken and NOT released; the six
     * bytes the call pushed are simply abandoned, because `unlk A6` at
     * 0x00E0969E restores SP.
     */
    ML_$LOCK(ML_LOCK_PMAP);
}

/*
 * AREA_$ASSOC - Associate one area page with a physical page
 *
 * Parameters (prologue 0x00E096B0-0x00E096B4 and the two call sites):
 *   area_id     A6+0x08 word - area id (D2), validated against
 *               AREA_$N_AREAS
 *   bste_idx    A6+0x0A word - the area-relative block index, passed BY
 *               ADDRESS to area_$find_entry_by_uid (0x00E096CE
 *               `pea (0xa,A6)`)
 *   page        A6+0x0C word - the page within that block; also the second
 *               argument of AST_$ASSOC_AREA
 *   ppn         A6+0x0E long - forwarded to AST_$ASSOC_AREA unchanged
 *   status_ret  A6+0x12 long - output status (A3)
 *
 * The routine never computes an area_$entry_t pointer: 0x00E096BA only
 * range-checks the id and everything else goes through the ASTE.
 *
 * Original address: 0x00E096A2
 */
void AREA_$ASSOC(int16_t area_id, uint16_t bste_idx, int16_t page,
                 uint32_t ppn, status_$t *status_ret)
{
    struct aste_t *aste_ptr;

    /*
     * 0x00E096B0-0x00E096BE.  `movea.l (0x12,A6),A3` between the `move.w`
     * and the `beq` does not touch the condition codes, so the zero test is
     * on area_id.
     */
    if ((uint16_t)area_id == 0 || (uint16_t)area_id > (uint16_t)AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return;
    }

    /* 0x00E096C8-0x00E096DC */
    aste_ptr = area_$find_entry_by_uid(area_id, &bste_idx, page, status_ret);

    /* 0x00E096DE */
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E096E2 */
    ML_$LOCK(ML_LOCK_PMAP);

    /* 0x00E096F0-0x00E09704 */
    AST_$ASSOC_AREA(aste_ptr->seg_index, page, ppn, status_ret);

    /* 0x00E09708 */
    aste_ptr->wire_count--;

    /* 0x00E0970C */
    ML_$UNLOCK(ML_LOCK_PMAP);
}
