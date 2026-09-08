/*
 * dir_$validate_pages - Validate and compact directory pages
 *
 * Validates the structural integrity of directory pages by walking them
 * backward from the last page. Checks that consecutive pages have
 * matching UIDs and valid entry types. If inconsistencies are found,
 * performs a cleanup pass to remove orphan pages and then truncates
 * the directory to the correct size.
 *
 * This function is called:
 * 1. From dir_$open_dir (directory open) when integrity issues are detected
 * 2. From DIR_$CLEANUP during process shutdown
 *
 * Page structure (offsets from page base returned by dir_$map_page):
 *   +0x00: flags byte (bits 7-6: entry type: 0=normal, 1=continuation, 2=overflow)
 *   +0x01: entry count (bits 5-0: count of active entries)
 *   +0x02: UID high (4 bytes)
 *   +0x06: UID low (4 bytes)
 *   +0x0A: page self-index (2 bytes)
 *   +0x0C: next page index (2 bytes)
 *   +0x14: offset to name table / B-tree root pointer
 *
 * Parameters:
 *   handle      - Directory handle (passed to dir_$map_page for page access)
 *   crash_flag  - If negative (bit 7 set), crash on errors instead of
 *                 returning status_$naming_internal_error
 *   status_ret  - Output: status code
 *
 * Returns:
 *   Result from dir_$truncate_pages (truncate operation) or error
 *
 * Original address: 0x00E53728
 * Original size: 752 bytes
 *
 * The validation logic has three phases:
 *   Phase 1: Walk backward finding the first valid page range
 *   Phase 2: Walk forward checking page cross-references
 *   Phase 3: Cleanup orphan pages and truncate
 */

#include "dir/dir_internal.h"

/*
 * Page header field access macros
 *
 * dir_$map_page returns a pointer to the page data in A0 (extraout_A0).
 * These macros extract fields from the page header.
 */
#define PAGE_ENTRY_TYPE(p)      ((*(uint8_t *)(p)) >> 6)
#define PAGE_ENTRY_COUNT(p)     ((*(uint8_t *)((char *)(p) + 1)) & 0x3F)
#define PAGE_UID_HIGH(p)        (*(uint32_t *)((char *)(p) + 2))
#define PAGE_UID_LOW(p)         (*(uint32_t *)((char *)(p) + 6))
#define PAGE_SELF_INDEX(p)      (*(uint16_t *)((char *)(p) + 10))
#define PAGE_NEXT_INDEX(p)      (*(uint16_t *)((char *)(p) + 12))
#define PAGE_BTREE_OFF(p)       (*(int16_t *)((char *)(p) + 0x14))

uint32_t dir_$validate_pages(void *handle, char crash_flag,
                              status_$t *status_ret)
{
    uint16_t total_pages;
    /* D6: the walk bound, total_pages - 1.  Phase 2 (`cmp.w D6w,D4w` /
     * `bcs` at 0x00E53952) and phase 3 (`cmp.w D6w,D2w` / `bcs` at
     * 0x00E539D0) both compare against THIS, not against total_pages. */
    uint16_t page_limit;
    uint16_t last_page;
    uint16_t cur_page;
    uint16_t start_page;
    /* A6-0x3E: set at 0x00E53948 whenever the walk finds a mismatch; the
     * `tst.b (-0x3e,A6)` / `bpl` at 0x00E53958 skips the whole copy loop
     * when the walk ended without one. */
    char mismatch_seen;
    uint32_t tracking_uid_high;
    uint32_t tracking_uid_low;
    char seen_forward;          /* local_3e */
    char seen_backward;         /* local_40 */
    void *page_data;

    *status_ret = status_$ok;

    /* dir_$handle_t.length is the directory's byte length; >> 10 is its
     * page count. */
    total_pages = (uint16_t)(((const dir_$handle_t *)handle)->length >> 10);
    page_limit = (uint16_t)(total_pages - 1);
    last_page = page_limit;
    cur_page = last_page;

    /* Initialize tracking UID to UID_$NIL */
    tracking_uid_high = UID_$NIL.high;
    tracking_uid_low = UID_$NIL.low;

    /*
     * Phase 1: Walk backward from last page, finding the boundary
     * of valid contiguous pages with matching UIDs.
     */
    for (;;) {
        page_data = dir_$map_page(handle, cur_page);

        /* If tracking UID is NIL, adopt this page's UID */
        if (tracking_uid_high == UID_$NIL.high &&
            tracking_uid_low == UID_$NIL.low) {
            tracking_uid_high = PAGE_UID_HIGH(page_data);
            tracking_uid_low = PAGE_UID_LOW(page_data);
        }

        /* Check entry type is valid (must be 0=normal or 1=continuation) */
        if ((PAGE_ENTRY_TYPE(page_data) != 0 &&
             PAGE_ENTRY_TYPE(page_data) != 1) ||
            PAGE_ENTRY_COUNT(page_data) == 0) {
            /* Invalid page - check if we've reached the starting point */
            if ((last_page & 0xFFFF) == cur_page) {
                break;  /* Reached start of scan with no valid range */
            }
            goto phase2_init;
        }

        /* Check if this page's UID matches the tracking UID */
        if (tracking_uid_high != PAGE_UID_HIGH(page_data) ||
            tracking_uid_low != PAGE_UID_LOW(page_data) ||
            cur_page == PAGE_SELF_INDEX(page_data)) {
            /* UID mismatch or self-referencing page */
            if ((last_page & 0xFFFF) == cur_page) {
                break;
            }
            goto phase2_init;
        }

        /* Page is valid and matches - continue backward */
        if (cur_page == 0) {
            goto error_internal;
        }
        cur_page--;
    }

    /*
     * All pages from cur_page to total-1 are empty or invalid.
     * Find the last page with actual entries.
     */
    for (;;) {
        page_data = dir_$map_page(handle, cur_page);
        if (PAGE_ENTRY_COUNT(page_data) != 0) {
            break;
        }
        cur_page--;
    }

    /* If the valid range fits within the total, just truncate */
    if ((last_page & 0xFFFF) < (uint32_t)cur_page + 1) {
        return 0;  /* No truncation needed */
    }
    goto do_truncate;

phase2_init:
    /* 0x00E53810-0x00E5381C: both direction flags start TRUE, the mismatch
     * flag starts FALSE, and `move.w D4w,D5w` copies the phase-1 walker
     * into the bound.  Phase 2 walks `last_page` (D4) and compares page
     * references against `cur_page` (D5); both start on the same page. */
    mismatch_seen = 0;
    seen_forward = -1;
    seen_backward = -1;
    last_page = cur_page;

phase2:
    /*
     * Phase 2: Walk forward from the break point, checking page
     * cross-references for structural consistency.
     *
     * This validates that pages that reference each other via their
     * index fields actually have matching UIDs.
     */
    /* 0x00E53952: `cmp.w D6w,D4w` / `bcs` - the loop runs while the walker
     * is strictly below total_pages - 1. */
    if (last_page >= page_limit) {
        /* 0x00E53958: without a mismatch the copy loop is skipped
         * entirely and start_page is never established. */
        if (mismatch_seen >= 0) {
            goto do_purify;
        }
        goto cleanup_start;
    }

    {
        uint16_t fwd_page = (uint16_t)last_page + 1;
        void *fwd_data;
        void *ref_data;

        last_page = (uint32_t)fwd_page;
        fwd_data = dir_$map_page(handle, fwd_page);

        /* Check if the back-reference page is within range */
        if (cur_page < ((uint16_t *)fwd_data)[5]) {
            goto error_internal;
        }

        /* Read the referenced page */
        ref_data = dir_$map_page(handle, ((uint16_t *)fwd_data)[5]);

        /* Check if referenced page's UID matches tracking UID */
        if (tracking_uid_high != PAGE_UID_HIGH(ref_data) ||
            tracking_uid_low != PAGE_UID_LOW(ref_data)) {
            /* UID mismatch - check if already seen this direction */
            if (seen_backward >= 0) {
                goto cleanup_start;
            }
            seen_forward = 0;
            goto phase2;
        }

        /* UIDs match - check if already seen forward */
        if (seen_forward >= 0) {
            goto cleanup_start;
        }
        seen_backward = 0;

        /* Check page type for B-tree vs flat structure */
        if ((*(uint16_t *)fwd_data & 0x1000) == 0) {
            /* Flat structure - check next page reference */
            /* 0x00E53920: cmp.w D6w,D4w / bcc - skip when fwd_page >= page_limit
             * (unsigned), D6 = page_limit = total_pages - 1. */
            if (fwd_page >= page_limit) {
                goto phase2;
            }
            if (cur_page < *(uint16_t *)((char *)ref_data + 0x0C)) {
                goto cleanup_start;
            }
            void *next_data = dir_$map_page(handle,
                *(uint16_t *)((char *)ref_data + 0x0C));
            if (tracking_uid_high != PAGE_UID_HIGH(next_data) ||
                tracking_uid_low != PAGE_UID_LOW(next_data)) {
                goto cleanup_start;
            }
        } else {
            /* B-tree structure - validate tree pointers */
            uint8_t entry_type = PAGE_ENTRY_TYPE(ref_data);
            if (entry_type != 1) {
                goto error_internal;
            }

            /* Navigate B-tree: get offset to name table, then read pointers */
            int16_t btree_off = PAGE_BTREE_OFF(ref_data);
            int16_t *idx_ptr = (int16_t *)((char *)ref_data + btree_off + 0x12);
            uint16_t left_idx = *(uint16_t *)((char *)ref_data + idx_ptr[0] + 2);
            uint16_t right_idx = *(uint16_t *)((char *)ref_data + idx_ptr[1] + 2);

            if (cur_page < left_idx || cur_page < right_idx) {
                goto cleanup_start;
            }

            /* Verify left subtree page UID */
            void *left_data = dir_$map_page(handle, left_idx);
            if (tracking_uid_high != PAGE_UID_HIGH(left_data) ||
                tracking_uid_low != PAGE_UID_LOW(left_data)) {
                goto cleanup_start;
            }

            /* Verify right subtree page UID */
            void *right_data = dir_$map_page(handle, right_idx);
            if (tracking_uid_high != PAGE_UID_HIGH(right_data) ||
                tracking_uid_low != PAGE_UID_LOW(right_data)) {
                goto cleanup_start;
            }
        }
        goto phase2;
    }

cleanup_start:
    /* 0x00E53948: `st (-0x3e,A6)` before the branch to 0x00E5395E. */
    mismatch_seen = -1;
    /*
     * Phase 3: Clean up orphan pages.
     * Walk forward from the start point, and for each page that
     * references a page with a matching UID, copy the current page
     * data over it and zero out the UID to mark it as cleaned.
     */
    /* 0x00E5395E: `move.w D5w,D2w` */
    start_page = cur_page;

    /* 0x00E539D0: `cmp.w D6w,D2w` / `bcs` */
    while (start_page < page_limit) {
        void *src_data;
        void *dst_data;
        uint16_t ref_idx;

        start_page++;
        src_data = dir_$map_page(handle, start_page);
        ref_idx = *(uint16_t *)((char *)src_data + 10);
        dst_data = dir_$map_page(handle, ref_idx);

        /* Check if referenced page's UID matches tracking UID */
        if (PAGE_UID_HIGH(dst_data) == tracking_uid_high &&
            PAGE_UID_LOW(dst_data) == tracking_uid_low) {
            /* Mark destination page as dirty */
            DIR_$WIRE_PAGE(handle, dst_data);

            /* Copy 256 uint32_t (1024 bytes = one full page) from src to dst */
            {
                uint32_t *dst = (uint32_t *)dst_data;
                uint32_t *src = (uint32_t *)src_data;
                int16_t n;
                for (n = 0xFF; n >= 0; n--) {
                    *dst++ = *src++;
                }
            }

            /* Zero out the UID in the destination to mark it cleaned */
            *(uint32_t *)((char *)dst_data + 2) = 0;

            /* Clear the "dirty" flag bit 4 */
            *(uint8_t *)dst_data &= 0xEF;

            /* Release/flush the page */
            dir_$release_wire(handle);
        }
    }

do_purify:
    /* Purify the directory (flush changes) */
    {
        uint32_t result;
        /* 0x00E539DC: move.l (-0x8,A5),-(SP) pushes the CONTENTS of the
         * A5-8 cell = &DIR_$CONST_ZERO_L (0x00E4B33C). */
        result = AST_$PURIFY(handle, 2, 0, &DIR_$CONST_ZERO_L, 0, status_ret);
        if (*status_ret != status_$ok) {
            return result;
        }
    }

do_truncate:
    /* Truncate the directory to the validated page count */
    return dir_$truncate_pages(handle, cur_page + 1, status_ret);

error_internal:
    if (crash_flag < 0) {
        CRASH_SYSTEM(DIR_$CRASH_STATUS);   /* move.l (-0x4,A5),-(SP) */
    }
    *status_ret = status_$naming_internal_error;
    return 0;
}
