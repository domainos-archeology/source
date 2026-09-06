/*
 * dir_$alloc_split_page - Allocate pages for B-tree splitting
 *
 * Allocates page numbers for B-tree page splitting during directory entry
 * insertion. The strategy is:
 *   1. Scan backward from end of directory to find the "gap" (free pages
 *      at the end of the directory's page space).
 *   2. Use gap pages for the extra split pages if sufficient.
 *   3. If more pages are needed, scan the directory's segment map bitmap
 *      (via AST_$GET_SEG_MAP) for free pages within the existing directory.
 *   4. If still not enough, extend the directory by growing past the gap.
 *   5. Fill split_pages[1..base_count] with consecutive page numbers
 *      starting at next_page_start.
 *   6. Copy pages from the B-tree path to the newly allocated pages,
 *      updating the directory UID in each copied page.
 *   7. Generate a new directory UID via UID_$GEN.
 *   8. Call dir_$purify_split_pages to sort and flush the allocations.
 *
 * Originally a nested Pascal subprocedure of dir_$insert_entry. Accesses
 * the insertion context (add_entry's frame) and also needs slot_idx and
 * base_offset from insert_entry's scope (originally accessed via the
 * parent Pascal frame pointer chain).
 *
 * Parameters:
 *   ctx         - Shared insertion context
 *   flag        - 0xFF for root split (allocates 2 extra pages),
 *                 0x00 for non-root (no extra pages)
 *   slot_idx    - Current B-tree level from insert_entry (parent's A6+0x08)
 *   base_offset - Index table base offset from insert_entry (parent's A6-0x14)
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E4EB40
 * Original size: 906 bytes
 */

#include "dir/dir_internal.h"

void dir_$alloc_split_page(dir_insert_ctx_t *ctx, uint8_t flag,
                           int16_t slot_idx, int16_t base_offset,
                           status_$t *status_ret)
{
    uint8_t *handle = (uint8_t *)(uintptr_t)ctx->handle;
    void *handle_ptr = (void *)(uintptr_t)ctx->handle;

    /* Store current slot index in context for finalize_split to read later.
     * Original: move.w (0x8,A0),(-0xa8,A2) */
    ctx->current_slot = slot_idx;

    /* Compute number of pages needed for the split.
     * flag=0xFF (root split): 2 extra pages (for both child nodes).
     * flag=0x00 (non-root): just the base path pages.
     * Original: D3 = max_depth - slot_idx + (flag < 0 ? 2 : 0) */
    int16_t pages_needed;
    if ((int8_t)flag < 0) {
        pages_needed = (ctx->max_depth - slot_idx) + 2;
    } else {
        pages_needed = ctx->max_depth - slot_idx;
    }

    /* base_count = max_depth - slot_idx + 1 (number of path levels to copy)
     * Original: D4 = max_depth - slot_idx + 1, stored in local[-0x6e] */
    int16_t base_count = (ctx->max_depth - slot_idx) + 1;

    /* Get total number of directory pages from handle.
     * handle[0x10] = directory size in bytes; each page is 1024 bytes.
     * Original: D5 = handle[0x10] >> 10 */
    uint16_t total_pages = (uint16_t)(*(uint32_t *)(handle + 0x10) >> 10);

    /* Scan backward from last page to find the last in-use page.
     * An in-use page has a non-zero first word (page header flags).
     * Original: loop at 0x00E4EBA0-0x00E4EBCA */
    int16_t first_free_page = (int16_t)(total_pages - 1);
    while (1) {
        uint8_t *page = (uint8_t *)dir_$map_page(handle_ptr, first_free_page);
        if (*(int16_t *)page != 0) {
            break;  /* Found last in-use page */
        }
        if (first_free_page == 0) {
            CRASH_SYSTEM((const status_$t *)&Naming_bad_request_header_ver_err);
        }
        first_free_page--;
    }
    /* Point past last used page to the first free page.
     * Original: addq.w #0x1,(-0x6c,A6) at 0x00E4EBCC */
    first_free_page++;

    /* Compute available gap pages and where new pages will start.
     * gap_count = total_pages - first_free_page (free pages at end).
     * Original: D2 = total_pages - first_free_page */
    int16_t next_page_start;  /* local[-0x6a] in original */
    int16_t gap_count = (int16_t)total_pages - first_free_page;
    int16_t remaining_gap;

    if (gap_count < base_count) {
        /* Not enough gap pages for base allocation.
         * next_page_start = first_free_page.
         * remaining_gap = 0 (all gap pages will be used for extras).
         * Original: D0 = total_pages - gap_count, D2 = 0 */
        next_page_start = first_free_page;
        remaining_gap = 0;
    } else {
        /* Enough gap pages. Allocate base_count pages from the bottom
         * of the gap.
         * Original: D0 = total_pages - base_count, D2 = gap - base_count */
        next_page_start = (int16_t)total_pages - base_count;
        remaining_gap = gap_count - base_count;
    }

    /* Now allocate the extra split pages (pages_needed pages beyond
     * the base_count pages). These go into split_pages[base_count+1..]. */
    int16_t split_idx = base_count;  /* D5: current index into split_pages */

    if (pages_needed <= remaining_gap) {
        /* === Simple case: all extra pages come from the gap ===
         * Original: branch at 0x00E4ED6A */
        int16_t count = pages_needed - 1;
        if (count >= 0) {
            int16_t offset = 1;
            do {
                ctx->split_pages[base_count + offset] =
                    first_free_page + offset - 1;
                offset++;
                count--;
            } while (count != -1);
        }
    } else {
        /* === Complex case: need more pages beyond the gap ===
         * Original: fall through at 0x00E4EC0C */

        /* First, use available gap pages.
         * Original: loop at 0x00E4EC24-0x00E4EC36 */
        {
            int16_t gap_remaining = remaining_gap - 1;
            if (gap_remaining >= 0) {
                int16_t offset = 1;
                do {
                    split_idx++;
                    ctx->split_pages[split_idx] =
                        first_free_page + offset - 1;
                    offset++;
                    gap_remaining--;
                } while (gap_remaining != -1);
            }
        }

        int16_t still_needed = pages_needed - remaining_gap;

        /* Map root page (page 0) and check for segment map.
         * Bit 11 (0x0800) of root page header indicates the directory
         * has a segment map bitmap that can be scanned for free pages.
         * Original: bsr dir_$map_page(handle, 0), btst #0xb at 0x00E4EC4E */
        uint8_t *root_page = (uint8_t *)dir_$map_page(handle_ptr, 0);

        if ((*(uint16_t *)root_page & 0x0800) != 0) {
            /* Directory has a segment map bitmap.
             * Scan for free pages within the existing directory.
             * Each segment = 32 pages, one 32-bit bitmap word per segment. */
            uint16_t seg_map_index = 0;

            /* Compute max segment group to scan.
             * Original: (first_free_page - 1) >> 5, with rounding for 0 case */
            int32_t temp = (int32_t)first_free_page - 1;
            if (temp < 0) {
                temp += 0x1F;
            }
            int16_t max_seg_group = (int16_t)(temp >> 5);

            int16_t page_base = 0;  /* D4: page number offset per segment */

            do {
                /* Read directory UID from handle for AST_$GET_SEG_MAP call.
                 * Original: move.l (A1)+,(-0x18,A6) / move.l (A1)+,(-0x14,A6) */
                uid_t local_uid;
                local_uid.high = *(uint32_t *)handle;
                local_uid.low = *(uint32_t *)(handle + 4);

                uint32_t seg_bitmap[8];  /* 32-byte bitmap buffer */
                status_$t seg_status;

                /* AST_$GET_SEG_MAP: get 1 segment's bitmap (32 pages).
                 * offset = seg_map_index * 32768 (seg_map_index << 15).
                 * Original: jsr AST_$GET_SEG_MAP at 0x00E4ECB2 */
                AST_$GET_SEG_MAP((uint32_t *)&local_uid,
                                 (uint32_t)seg_map_index << 15,
                                 0, (uid_t *)1, 0x20, 2,
                                 seg_bitmap, &seg_status);
                if (seg_status != status_$ok) {
                    *status_ret = seg_status;
                    return;
                }

                /* Scan 32 bits of the bitmap for free pages.
                 * Bit clear = page is free, bit set = page is in use.
                 * Original: inner loop at 0x00E4ECDE-0x00E4ED0E,
                 * outer word loop runs exactly once (1 dword = 32 pages). */
                uint32_t bitmap_word = seg_bitmap[0];
                int16_t bit;

                for (bit = 0; bit <= 31; bit++) {
                    if ((bitmap_word & (1u << bit)) == 0) {
                        /* Page is free */
                        uint16_t page_num = (uint16_t)(bit + page_base);

                        /* Can't use pages >= first_free_page (they're in the
                         * gap) or page 0 (root page). In either case, clear
                         * bit 3 of root page header and stop scanning.
                         * Original: bcc/beq at 0x00E4ECF0-0x00E4ECFA */
                        if (page_num >= (uint16_t)first_free_page ||
                            page_num == 0) {
                            root_page[0] &= ~0x08;
                            goto done_seg_scan;
                        }

                        split_idx++;
                        ctx->split_pages[split_idx] = (int16_t)page_num;
                        still_needed--;
                        if (still_needed == 0) {
                            goto done_seg_scan;
                        }
                    }
                }

done_seg_scan:
                seg_map_index++;
                page_base += 32;
            } while (seg_map_index <= (uint16_t)max_seg_group &&
                     still_needed != 0);
        }

        /* If still need more pages, extend the directory by allocating
         * page numbers past the current next_page_start.
         * Original: loop at 0x00E4ED50-0x00E4ED60, then add at 0x00E4ED64 */
        if (still_needed != 0) {
            int16_t ext_count = still_needed - 1;
            if (ext_count >= 0) {
                int16_t offset = 1;
                do {
                    split_idx++;
                    ctx->split_pages[split_idx] =
                        next_page_start + offset - 1;
                    offset++;
                    ext_count--;
                } while (ext_count != -1);
            }
            next_page_start += still_needed;
        }
    }

    /* Fill base pages: split_pages[1..base_count] with consecutive
     * page numbers starting at next_page_start.
     * Original: loop at 0x00E4ED9C-0x00E4EDB0 */
    {
        int16_t count = base_count - 1;
        if (count >= 0) {
            int16_t offset = 1;
            do {
                ctx->split_pages[offset] = next_page_start + offset - 1;
                offset++;
                count--;
            } while (count != -1);
        }
    }

    /* Update directory size if needed.
     * new_size = (base_count + next_page_start) * 1024.
     * Original: lsl.l #0x8 + lsl.l #0x2, cmp.l at 0x00E4EDCA */
    {
        int32_t new_size = ((int32_t)base_count + (int32_t)next_page_start) << 10;
        if (new_size > (int32_t)*(uint32_t *)(handle + 0x10)) {
            *(uint32_t *)(handle + 0x10) = (uint32_t)new_size;
        }
    }

    /* Generate new directory UID.
     * Original: pea (-0x50,A2); jsr UID_$GEN at 0x00E4EDDC */
    UID_$GEN((uid_t *)&ctx->dir_uid_high);

    /* Copy pages from B-tree path to newly allocated locations.
     * Iterates from max_depth down to slot_idx, copying each page
     * to consecutive destination pages starting at next_page_start.
     * Original: loop at 0x00E4EE0A-0x00E4EE84 */
    {
        int16_t dest_page = next_page_start;
        int16_t level = ctx->max_depth;
        int16_t loop_count = ctx->max_depth - slot_idx;

        if (loop_count >= 0) {
            do {
                /* Map destination page */
                uint8_t *dest = (uint8_t *)dir_$map_page(
                    handle_ptr, dest_page);

                /* Map source page from B-tree path.
                 * Original: move.w (-0x74,A1),-(SP) where A1 walks path */
                ctx->page_data = (uint8_t *)dir_$map_page(
                    handle_ptr, ctx->path_page[level]);

                /* Copy entire page (1024 bytes = 256 longwords).
                 * Original: move.l (A3)+,(A4)+; dbf D1w at 0x00E4EE4E */
                {
                    uint32_t *src32 = (uint32_t *)ctx->page_data;
                    uint32_t *dst32 = (uint32_t *)dest;
                    int16_t i;
                    for (i = 0; i < 256; i++) {
                        dst32[i] = src32[i];
                    }
                }

                /* Update directory UID in the destination page header.
                 * Original: move.l (A4)+,(0x2,A2) / move.l (A4)+,(0x6,A2) */
                *(uint32_t *)(dest + 2) = ctx->dir_uid_high;
                *(uint32_t *)(dest + 6) = ctx->dir_uid_low;

                /* Set/clear bit 4 (0x10) of the destination page header.
                 * Bit 4 is set only when level == 1 (one above leaf) AND
                 * flag has bit 7 set (root split). This marks the split page.
                 * Original: seq/and/lsr/lsl at 0x00E4EE64-0x00E4EE76 */
                {
                    uint8_t cond = (level == 1) ? 0xFF : 0x00;
                    cond &= flag;
                    cond >>= 7;
                    dest[0] &= 0xEF;  /* Clear bit 4 */
                    cond <<= 4;
                    dest[0] |= cond;
                }

                dest_page++;
                level--;
                loop_count--;
            } while (loop_count != -1);
        }
    }

    /* Set page_count = base_count.
     * Original: move.w (-0x6e,A6),(-0xa6,A2) at 0x00E4EE8A */
    ctx->page_count = base_count;

    /* Sort split pages and call AST_$PURIFY to flush them.
     * Original: bsr dir_$purify_split_pages (0x00e4ea9c) at 0x00E4EE98 */
    dir_$purify_split_pages(ctx, status_ret);

    if (*status_ret == status_$ok) {
        /* Set handle overflow/split flag.
         * Original: st (0xe,A3) at 0x00E4EEAA */
        *(handle + 0x0E) = 0xFF;

        /* Set idx_base = page_data + base_offset.
         * base_offset is insert_entry's local_base_offset, accessed
         * in the original via parent frame at (-0x14,A0).
         * Original: ext.l D1; add.l (-0x94,A2),D1 at 0x00E4EEB6 */
        ctx->idx_base = ctx->page_data + (int32_t)base_offset;
    }
}
