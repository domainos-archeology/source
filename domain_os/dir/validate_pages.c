/*
 * DIR_$VALIDATE_PAGES - Validate and compact directory pages
 *
 * Validates the structural integrity of directory pages by walking them
 * backward from the last page. Checks that consecutive pages have
 * matching UIDs and valid entry types. If inconsistencies are found,
 * performs a cleanup pass to remove orphan pages and then truncates
 * the directory to the correct size.
 *
 * This function is called:
 * 1. From FUN_00e4ba02 (directory open) when integrity issues are detected
 * 2. From DIR_$CLEANUP during process shutdown
 *
 * Page structure (offsets from page base returned by FUN_00e4b340):
 *   +0x00: flags byte (bits 7-6: entry type: 0=normal, 1=continuation, 2=overflow)
 *   +0x01: entry count (bits 5-0: count of active entries)
 *   +0x02: UID high (4 bytes)
 *   +0x06: UID low (4 bytes)
 *   +0x0A: page self-index (2 bytes)
 *   +0x0C: next page index (2 bytes)
 *   +0x14: offset to name table / B-tree root pointer
 *
 * Parameters:
 *   handle      - Directory handle (passed to FUN_00e4b340 for page access)
 *   crash_flag  - If negative (bit 7 set), crash on errors instead of
 *                 returning status_$naming_internal_error
 *   status_ret  - Output: status code
 *
 * Returns:
 *   Result from FUN_00e4e90a (truncate operation) or error
 *
 * Original address: 0x00E53728
 * Original size: 752 bytes
 *
 * TODO: This is a complex function with many Ghidra decompiler artifacts
 * (extraout_A0 return values from FUN_00e4b340). The page structure needs
 * further analysis. The validation logic has three phases:
 *   Phase 1: Walk backward finding the first valid page range
 *   Phase 2: Walk forward checking page cross-references
 *   Phase 3: Cleanup orphan pages and truncate
 */

#include "dir/dir_internal.h"

/*
 * Page header field access macros
 *
 * FUN_00e4b340 returns a pointer to the page data in A0 (extraout_A0).
 * These macros extract fields from the page header.
 */
#define PAGE_ENTRY_TYPE(p)      ((*(uint8_t *)(p)) >> 6)
#define PAGE_ENTRY_COUNT(p)     ((*(uint8_t *)((char *)(p) + 1)) & 0x3F)
#define PAGE_UID_HIGH(p)        (*(uint32_t *)((char *)(p) + 2))
#define PAGE_UID_LOW(p)         (*(uint32_t *)((char *)(p) + 6))
#define PAGE_SELF_INDEX(p)      (*(uint16_t *)((char *)(p) + 10))
#define PAGE_NEXT_INDEX(p)      (*(uint16_t *)((char *)(p) + 12))
#define PAGE_BTREE_OFF(p)       (*(int16_t *)((char *)(p) + 0x14))

uint32_t DIR_$VALIDATE_PAGES(void *handle, char crash_flag,
                              status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
    uint16_t total_pages;
    uint16_t last_page;
    uint16_t cur_page;
    uint16_t start_page;
    uint32_t tracking_uid_high;
    uint32_t tracking_uid_low;
    char seen_forward;          /* local_3e */
    char seen_backward;         /* local_40 */
    void *page_data;

    *status_ret = status_$ok;

    /* Get total page count from the directory handle metadata */
    /* handle[2].high >> 10 gives the page count */
    total_pages = (uint16_t)((*(uint32_t *)((char *)handle + 0x10)) >> 10);
    last_page = total_pages - 1;
    cur_page = last_page;

    /* Initialize tracking UID to UID_$NIL */
    tracking_uid_high = UID_$NIL.high;
    tracking_uid_low = UID_$NIL.low;

    /*
     * Phase 1: Walk backward from last page, finding the boundary
     * of valid contiguous pages with matching UIDs.
     */
    for (;;) {
        page_data = FUN_00e4b340(handle, cur_page);

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
            seen_forward = -1;
            seen_backward = -1;
            last_page = cur_page;
            goto phase2;
        }

        /* Check if this page's UID matches the tracking UID */
        if (tracking_uid_high != PAGE_UID_HIGH(page_data) ||
            tracking_uid_low != PAGE_UID_LOW(page_data) ||
            cur_page == PAGE_SELF_INDEX(page_data)) {
            /* UID mismatch or self-referencing page */
            if ((last_page & 0xFFFF) == cur_page) {
                break;
            }
            seen_forward = -1;
            seen_backward = -1;
            last_page = cur_page;
            goto phase2;
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
        page_data = FUN_00e4b340(handle, cur_page);
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

phase2:
    /*
     * Phase 2: Walk forward from the break point, checking page
     * cross-references for structural consistency.
     *
     * This validates that pages that reference each other via their
     * index fields actually have matching UIDs.
     */
    if (total_pages <= (uint16_t)last_page) {
        goto phase3;
    }

    {
        uint16_t fwd_page = (uint16_t)last_page + 1;
        void *fwd_data;
        void *ref_data;

        last_page = (uint32_t)fwd_page;
        fwd_data = FUN_00e4b340(handle, fwd_page);

        /* Check if the back-reference page is within range */
        if (cur_page < ((uint16_t *)fwd_data)[5]) {
            goto error_internal;
        }

        /* Read the referenced page */
        ref_data = FUN_00e4b340(handle, ((uint16_t *)fwd_data)[5]);

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
            if (total_pages <= fwd_page) {
                goto phase2;
            }
            if (cur_page < *(uint16_t *)((char *)ref_data + 0x0C)) {
                goto cleanup_start;
            }
            void *next_data = FUN_00e4b340(handle,
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
            void *left_data = FUN_00e4b340(handle, left_idx);
            if (tracking_uid_high != PAGE_UID_HIGH(left_data) ||
                tracking_uid_low != PAGE_UID_LOW(left_data)) {
                goto cleanup_start;
            }

            /* Verify right subtree page UID */
            void *right_data = FUN_00e4b340(handle, right_idx);
            if (tracking_uid_high != PAGE_UID_HIGH(right_data) ||
                tracking_uid_low != PAGE_UID_LOW(right_data)) {
                goto cleanup_start;
            }
        }
        goto phase2;
    }

cleanup_start:
    /*
     * Phase 3: Clean up orphan pages.
     * Walk forward from the start point, and for each page that
     * references a page with a matching UID, copy the current page
     * data over it and zero out the UID to mark it as cleaned.
     */
    start_page = cur_page;

phase3:
    while (start_page < total_pages) {
        void *src_data;
        void *dst_data;
        uint16_t ref_idx;

        start_page++;
        src_data = FUN_00e4b340(handle, start_page);
        ref_idx = *(uint16_t *)((char *)src_data + 10);
        dst_data = FUN_00e4b340(handle, ref_idx);

        /* Check if referenced page's UID matches tracking UID */
        if (PAGE_UID_HIGH(dst_data) == tracking_uid_high &&
            PAGE_UID_LOW(dst_data) == tracking_uid_low) {
            /* Mark destination page as dirty */
            FUN_00e4b7b6(handle, dst_data);

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
            FUN_00e4b838(handle);
        }
    }

    /* Purify the directory (flush changes) */
    {
        uint32_t result;
        result = AST_$PURIFY(handle, 2, 0, (uint32_t *)(a5 - 8), 0, status_ret);
        if (*status_ret != status_$ok) {
            return result;
        }
    }

do_truncate:
    /* Truncate the directory to the validated page count */
    return FUN_00e4e90a(handle, cur_page + 1, status_ret);

error_internal:
    if (crash_flag < 0) {
        CRASH_SYSTEM(*(status_$t **)(a5 - 4));
    }
    *status_ret = status_$naming_internal_error;
    return 0;
}
