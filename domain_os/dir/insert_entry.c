/*
 * dir_$insert_entry - Core B-tree entry insertion for directory pages
 *
 * Originally a nested Pascal subprocedure of dir_$add_entry. This is the
 * core low-level function that inserts a new entry into a directory's B-tree
 * page structure. It handles:
 *   - Finding available space on the target page
 *   - Compacting dead entries to reclaim space (if page has reclaimable bit)
 *   - Page splitting when the page is full (allocating new pages and
 *     redistributing entries)
 *   - Recursive insertion when splitting propagates up the B-tree
 *   - Updating page headers, index tables, and parent UID fields
 *
 * The function operates on a shared context (dir_insert_ctx_t) that contains
 * both the parent function's parameters and working variables used during
 * the insertion process. In the original M68K code, this context was the
 * parent's stack frame, accessed via A1 = parent A6.
 *
 * Parameters:
 *   ctx        - Shared insertion context (parent frame variables)
 *   slot_idx   - Current B-tree level / slot index for insertion
 *   param_2    - Recursive parameter (link data offset or 0 for root call)
 *   name_len   - Entry name length for this level
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4F3BA
 * Original size: 2640 bytes
 */

#include "dir/dir_internal.h"

/*
 * The page header is re-read from memory at every decision point, exactly as
 * the original does: dir_$compact_page_entries and the split helpers rewrite
 * the page in place, so a cached copy of any header field would go stale.
 */
static dir_page_hdr_t *dir_page_hdr(uint8_t *page)
{
    return (dir_page_hdr_t *)page;
}

/* The first two header bytes are written as one word and then patched with
 * byte operations; `btst #13` therefore tests bit 5 of the low (version)
 * byte.  Rebuilding the word here keeps the test endian-independent. */
static uint16_t dir_page_flags(const dir_page_hdr_t *hdr)
{
    return (uint16_t)(((uint16_t)hdr->kind << 8) | hdr->version);
}

static uint16_t dir_page_kind(const dir_page_hdr_t *hdr)
{
    return (uint16_t)((hdr->kind & DIR_PAGE_KIND_MASK) >> DIR_PAGE_KIND_SHIFT);
}

void dir_$insert_entry(dir_insert_ctx_t *ctx, int16_t slot_idx,
                       uint32_t param_2, int16_t name_len,
                       status_$t *status_ret)
{
    dir_page_hdr_t *page;
    dir_page_hdr_t *newp;
    int16_t local_base_offset;
    int16_t local_entry_size;

    *status_ret = status_$ok;

    /* 0xE4F3D0: map the target page using the B-tree path for this level */
    ctx->page_data = (uint8_t *)dir_$map_page(
        NAME_$HANDLE_TO_PTR(ctx->handle),
        ctx->path_page[slot_idx]);
    page = dir_page_hdr(ctx->page_data);
    int16_t target_entry_idx = ctx->path_entry[slot_idx];

    /* 0xE4F40A: page_no == 0 identifies the root page. */
    if (page->page_no == 0) {
        /* Root page - base offset includes the variable-length root area */
        ctx->inter_page = ctx->page_data;
        local_base_offset = *(int16_t *)(ctx->inter_page + DIR_PAGE_ROOT_AREA_LEN) +
                            DIR_PAGE_HDR_SIZE;
    } else {
        local_base_offset = DIR_PAGE_HDR_SIZE;
    }

    /* 0xE4F430: number of entries on this page */
    int32_t diff = (int32_t)(int16_t)page->index_end -
                   (int32_t)local_base_offset;
    if (diff < 0) diff += 1;
    int16_t num_entries = (int16_t)(diff >> 1);

    /* Compute index base pointer */
    ctx->idx_base = ctx->page_data + local_base_offset;

    /* 0xE4F462: required entry size depends on the page kind */
    if (dir_page_kind(page) == 0) {
        /* Leaf page - size depends on entry type */
        local_entry_size = DIR_$NAME_OFFSET_TABLE[ctx->entry_type] + name_len;
        if (ctx->entry_type == 4 && ctx->overflow_page == -1) {
            local_entry_size += ctx->link_len;
        }
    } else {
        /* Internal page - just name + 4 bytes for child pointer */
        local_entry_size = name_len + 4;
    }
    uint16_t aligned_size = (local_entry_size + 3) & 0xFFFC;

    /* 0xE4F4C0: free space on the page */
    ctx->free_space = (int16_t)page->heap_base - (int16_t)page->index_end;

    /* 0xE4F4D4: reclaim space if needed and the page has dead entries */
    if (ctx->free_space < aligned_size + 2 &&
        (dir_page_flags(page) & DIR_PAGE_RECLAIMABLE) != 0) {
        DIR_$WIRE_PAGE(NAME_$HANDLE_TO_PTR(ctx->handle), ctx->page_data);
        dir_$compact_page_entries(ctx);
        dir_$release_wire(NAME_$HANDLE_TO_PTR(ctx->handle));
        /* 0xE4F512: the header is re-read - compaction rewrote the page. */
        ctx->free_space = (int16_t)page->heap_base - (int16_t)page->index_end;
    }

    if (ctx->free_space < aligned_size + 2) {
        /*
         * Page is full - need to split.
         * This is the complex path that handles B-tree page splitting.
         */
        int16_t split_threshold = 0x1F7;

        /* 0xE4F548: page_no is read from the page again here - if the page
         * was compacted above, the cached value from 0xE4F40A is stale. */
        if (page->page_no == 0) {
            /* Root page split */
            if (ctx->max_depth == 8) {
                *status_ret = status_$directory_is_full;    /* 0xE4F55A */
                return;
            }
            ctx->inter_page = ctx->page_data;
            int16_t root_extra = *(int16_t *)(ctx->inter_page + DIR_PAGE_ROOT_AREA_LEN);
            if (root_extra < 0) root_extra += 1;
            split_threshold -= (root_extra >> 1);
        }

        /* Find the split point by accumulating entry sizes */
        int16_t accum_size = 0;
        int16_t split_entry = 0;
        int8_t found_insert_point = 0;
        int32_t idx_offset = 0;

        do {
            int8_t prev_found = found_insert_point;
            if (split_entry == target_entry_idx) {
                found_insert_point = -1;
                accum_size += aligned_size;
                prev_found = -1;
                if (accum_size >= split_threshold) break;
            }
            found_insert_point = prev_found;
            split_entry++;
            idx_offset += 2;
            if (num_entries < split_entry) {
                CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
            }
            ctx->temp_entry = ctx->page_data +
                (int32_t)*(int16_t *)(ctx->idx_base + idx_offset - 2);
            int16_t entry_sz = dir_$calc_entry_size(ctx->temp_entry);
            accum_size += entry_sz;
        } while (accum_size < split_threshold);

        /* 0xE4F600: and once more, for the root/non-root split decision. */
        if (page->page_no == 0) {
            /*
             * Root page split - allocate two new pages and make root an
             * internal node. This is the most complex code path.
             */
            dir_$alloc_split_page(ctx, 0xFF, slot_idx, local_base_offset, status_ret);
            if (*status_ret != status_$ok) return;

            /* Allocate first new child page */
            ctx->page_count++;
            ctx->new_page = (uint8_t *)dir_$map_page(
                NAME_$HANDLE_TO_PTR(ctx->handle),
                ctx->split_pages[ctx->page_count]);

            /* 0xE4F642: initialize the new page header */
            newp = dir_page_hdr(ctx->new_page);
            newp->kind = 0;                                     /* clr.w (A3) */
            newp->version = 0;
            /* Copy the page kind from the root */
            newp->kind = (uint8_t)((newp->kind & DIR_PAGE_VERSION_MASK) |
                                   (page->kind & DIR_PAGE_KIND_MASK));
            /* Copy the format version byte */
            uint8_t ver_byte = page->version & DIR_PAGE_VERSION_MASK;
            newp->version = (uint8_t)((newp->version & DIR_PAGE_KIND_MASK) | ver_byte);
            /* Copy directory UID into page header */
            newp->dir_uid_high = ctx->dir_uid_high;
            newp->dir_uid_low = ctx->dir_uid_low;
            /* Set page number and next-page link */
            newp->page_no = ctx->split_pages[ctx->page_count];
            newp->next_page = (uint16_t)(newp->page_no + 1);
            newp->index_end = DIR_PAGE_HDR_SIZE;
            newp->heap_base = DIR_PAGE_SIZE;

            DIR_$WIRE_PAGE(NAME_$HANDLE_TO_PTR(ctx->handle), ctx->page_data);

            /* Determine starting entry for copy based on page kind */
            int16_t copy_start;
            if (dir_page_kind(page) == 0) {
                copy_start = 2;  /* Leaf page: skip first 2 entries */
            } else {
                copy_start = 1;  /* Internal page: skip 1 entry */
            }

            int16_t move_from;
            if (found_insert_point < 0) {
                /* Insert point is before split point */
                dir_$move_entries_to_page(ctx, copy_start, target_entry_idx);
                if (dir_page_kind(page) == 0) {
                    copy_start = target_entry_idx;
                } else {
                    copy_start = target_entry_idx + 1;
                }
                dir_$write_entry_to_page(ctx, 0xFF, &ctx->new_page, copy_start,
                                         name_len, aligned_size, param_2);
                move_from = target_entry_idx + 1;
            } else {
                move_from = copy_start;
            }
            dir_$move_entries_to_page(ctx, move_from, split_entry);

            /* Allocate second new child page */
            ctx->page_count++;
            ctx->new_page = (uint8_t *)dir_$map_page(
                NAME_$HANDLE_TO_PTR(ctx->handle),
                ctx->split_pages[ctx->page_count]);

            /* Initialize second page header */
            newp = dir_page_hdr(ctx->new_page);
            newp->kind = 0;
            newp->version = 0;
            newp->kind = (uint8_t)((newp->kind & DIR_PAGE_VERSION_MASK) |
                                   (page->kind & DIR_PAGE_KIND_MASK));
            ver_byte = page->version & DIR_PAGE_VERSION_MASK;
            newp->version = (uint8_t)((newp->version & DIR_PAGE_KIND_MASK) | ver_byte);
            newp->dir_uid_high = ctx->dir_uid_high;
            newp->dir_uid_low = ctx->dir_uid_low;
            newp->page_no = ctx->split_pages[ctx->page_count];
            newp->next_page = 0xFFFF;   /* Last page marker */
            newp->index_end = DIR_PAGE_HDR_SIZE;
            newp->heap_base = DIR_PAGE_SIZE;

            if (found_insert_point < 0) {
                move_from = split_entry + 1;
            } else {
                /* Insert point is after split point */
                dir_$move_entries_to_page(ctx, split_entry + 1, target_entry_idx);
                move_from = target_entry_idx + 1;
                dir_$write_entry_to_page(ctx, 0, &ctx->new_page,
                                         move_from - split_entry,
                                         name_len, aligned_size, param_2);
            }
            dir_$move_entries_to_page(ctx, move_from, num_entries);

            /* Convert root page to internal node */
            page->kind = 0;
            page->version = 0;
            page->version = (uint8_t)((page->version & DIR_PAGE_KIND_MASK) | 5);
            page->kind = (uint8_t)((page->kind & DIR_PAGE_VERSION_MASK) | 0x40);
            page->index_end = (uint16_t)(local_base_offset + 4);
            page->heap_base = DIR_PAGE_SIZE;
            page->heap_base = (uint16_t)(((int16_t)page->heap_base - 6) & 0xFFFC);

            /* Create first internal entry (pointer to first child) */
            *(uint16_t *)(ctx->idx_base) = page->heap_base;
            ctx->temp_entry = ctx->page_data + (int32_t)(int16_t)page->heap_base;
            *(uint16_t *)ctx->temp_entry = 0;
            *ctx->temp_entry = (*ctx->temp_entry & 0xF8) | 1;
            *(ctx->temp_entry + 1) = 1;
            /* Child page is the previous split page (page_count - 1) */
            *(uint16_t *)(ctx->temp_entry + 2) =
                ctx->split_pages[ctx->page_count - 1];
            *(ctx->temp_entry + 4) = 0;

            /* Build second internal entry using first entry of first child */
            uint8_t *first_child_idx = ctx->new_page + DIR_PAGE_HDR_SIZE;
            uint8_t *first_child_entry = ctx->new_page +
                (int32_t)*(int16_t *)first_child_idx;
            uint8_t *first_child_name = first_child_entry +
                DIR_$NAME_OFFSET_TABLE[*first_child_entry & 7];

            /* Allocate space for second internal entry */
            page->heap_base = (uint16_t)(((int16_t)page->heap_base - 4 -
                 (uint16_t)*(first_child_entry + 1)) & 0xFFFC);

            *(uint16_t *)(ctx->idx_base + 2) = page->heap_base;
            ctx->temp_entry = ctx->page_data + (int32_t)(int16_t)page->heap_base;
            *(uint16_t *)ctx->temp_entry = 0;
            *ctx->temp_entry = (*ctx->temp_entry & 0xF8) | 1;
            *(ctx->temp_entry + 1) = *(first_child_entry + 1);
            /* Child page is the current split page */
            *(uint16_t *)(ctx->temp_entry + 2) =
                ctx->split_pages[ctx->page_count];

            /* Copy name from first entry of second child.
             * Pascal `for j := 1 to len` compiled as "count-1; if >= 0 then
             * repeat until count wraps to -1"; the count is a 16-bit signed
             * quantity, so test it as int16_t rather than shifting it into
             * the top half of a longword. */
            {
                int16_t nlen = (int16_t)(*(ctx->temp_entry + 1) - 1);
                if (nlen >= 0) {
                    int16_t j = 1;
                    do {
                        *(ctx->temp_entry + 3 + j) = first_child_name[j - 1];
                        j++;
                        nlen--;
                    } while (nlen != -1);
                }
            }
        } else {
            /*
             * Non-root page split - recurse to insert at the next level up,
             * then handle the split at this level.
             */
            uint32_t recursive_param;
            int16_t recursive_name_len;

            if (split_entry == target_entry_idx && found_insert_point >= 0) {
                /* Insert point IS the split point - use original params */
                recursive_param = param_2;
                recursive_name_len = name_len;
            } else {
                /* Split point is at an existing entry */
                if (split_entry + 1 > num_entries) {
                    CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
                }
                ctx->temp_entry = ctx->page_data +
                    (int32_t)*(int16_t *)(ctx->idx_base + split_entry * 2);
                /* 0xE4FA9C: the page number is read from the page again. */
                recursive_param =
                    (int32_t)DIR_$NAME_OFFSET_TABLE[*ctx->temp_entry & 7] +
                    (uint32_t)page->page_no * DIR_PAGE_SIZE +
                    (int32_t)*(int16_t *)(ctx->idx_base + split_entry * 2);
                recursive_name_len = (uint16_t)*(ctx->temp_entry + 1);
            }

            /* Recurse up one level */
            dir_$insert_entry(ctx, slot_idx - 1, recursive_param,
                              recursive_name_len, status_ret);
            if (*status_ret != status_$ok) return;

            /* Re-map the page after recursion (may have been invalidated) */
            ctx->page_data = (uint8_t *)dir_$map_page(
                NAME_$HANDLE_TO_PTR(ctx->handle),
                ctx->path_page[slot_idx]);
            page = dir_page_hdr(ctx->page_data);
            ctx->idx_base = ctx->page_data + local_base_offset;

            /* Allocate new page for the split */
            ctx->page_count++;
            ctx->new_page = (uint8_t *)dir_$map_page(
                NAME_$HANDLE_TO_PTR(ctx->handle),
                ctx->split_pages[ctx->page_count]);

            /* Initialize new page from current page */
            newp = dir_page_hdr(ctx->new_page);
            newp->kind = page->kind;            /* flags word copied whole */
            newp->version = page->version;
            newp->dir_uid_high = ctx->dir_uid_high;
            newp->dir_uid_low = ctx->dir_uid_low;
            newp->page_no = ctx->split_pages[ctx->page_count];
            newp->next_page = page->next_page;
            newp->index_end = DIR_PAGE_HDR_SIZE;
            newp->heap_base = DIR_PAGE_SIZE;

            DIR_$WIRE_PAGE(NAME_$HANDLE_TO_PTR(ctx->handle), ctx->page_data);

            int16_t move_from;
            if (found_insert_point >= 0) {
                /* Insert point is after split point */
                dir_$move_entries_to_page(ctx, split_entry + 1, target_entry_idx);
                move_from = target_entry_idx + 1;
                dir_$write_entry_to_page(ctx, 0, &ctx->new_page,
                                         move_from - split_entry,
                                         name_len, aligned_size, param_2);
            } else {
                move_from = split_entry + 1;
            }
            dir_$move_entries_to_page(ctx, move_from, num_entries);

            /* Update current page entry count */
            page->index_end = (uint16_t)(local_base_offset + split_entry * 2);
            num_entries = split_entry;

            dir_$compact_page_entries(ctx);

            /* If insert point was before split point, insert here */
            if (found_insert_point < 0) {
                int16_t new_count = split_entry + 1;
                /* Shift index entries to make room (16-bit signed count) */
                int16_t shift_count = (int16_t)(new_count - (target_entry_idx + 2));
                if (shift_count >= 0) {
                    int16_t si = new_count * 2;
                    int32_t di = new_count * 2;
                    do {
                        *(int16_t *)(ctx->idx_base + di - 2) =
                            *(int16_t *)(ctx->idx_base + si - 4);
                        di -= 2;
                        si -= 2;
                        shift_count--;
                    } while (shift_count != -1);
                }
                dir_$write_entry_to_page(ctx, 0, &ctx->page_data,
                                         target_entry_idx + 1,
                                         name_len, aligned_size, param_2);
            }

            /* Update page's next-page pointer */
            page->next_page = ctx->split_pages[ctx->page_count];
        }

        /* Update directory UID in page header */
        page->dir_uid_high = ctx->dir_uid_high;
        page->dir_uid_low = ctx->dir_uid_low;

        dir_$release_wire(NAME_$HANDLE_TO_PTR(ctx->handle));

        /* If we've reached the original slot level, finalize */
        if (ctx->max_depth == slot_idx) {
            dir_$finalize_split(ctx, status_ret);
        }
    } else {
        /*
         * Simple case - enough space on the page for the new entry.
         * Wire the page, shift index entries, and write the entry.
         */
        if (dir_page_kind(page) == 1) {
            /* Internal page - may need to allocate split page first */
            dir_$alloc_split_page(ctx, 0, slot_idx, local_base_offset, status_ret);
            if (*status_ret != status_$ok) return;
        }

        DIR_$WIRE_PAGE(NAME_$HANDLE_TO_PTR(ctx->handle), ctx->page_data);

        /* Shift index entries from target_entry_idx+1 to num_entries */
        num_entries++;
        {
            int16_t shift_count = (int16_t)(num_entries - (target_entry_idx + 2));
            if (shift_count >= 0) {
                int16_t si = num_entries * 2;
                int32_t di = num_entries * 2;
                do {
                    *(int16_t *)(ctx->idx_base + di - 2) =
                        *(int16_t *)(ctx->idx_base + si - 4);
                    di -= 2;
                    si -= 2;
                    shift_count--;
                } while (shift_count != -1);
            }
        }

        /* Handle FIM cleanup for type 4 entries on non-volatile dirs */
        int32_t fim_status = 0;
        if (*(int8_t *)((char *)NAME_$HANDLE_TO_PTR(ctx->handle) + 0x0E) >= 0 &&
            ctx->entry_type == 4) {
            fim_status = FIM_$CLEANUP(ctx->fim_data);
        }

        if (fim_status == 0 || fim_status == 0x120035) {
            dir_$write_entry_to_page(ctx, 0, &ctx->page_data,
                                     target_entry_idx + 1,
                                     name_len, aligned_size, param_2);
            if (fim_status == 0x120035) {
                FIM_$RLS_CLEANUP(ctx->fim_data);
            }
        } else {
            dir_$release_wire(NAME_$HANDLE_TO_PTR(ctx->handle));
            dir_$remove_entry(NAME_$HANDLE_TO_PTR(ctx->handle),
                              ctx->name, ctx->name_len, 4,
                              ctx->remove_uid, status_ret);
            FIM_$SIGNAL(fim_status);
        }

        /* Update directory UID in page header for volatile directories */
        if (*(int8_t *)((char *)NAME_$HANDLE_TO_PTR(ctx->handle) + 0x0E) < 0) {
            page->dir_uid_high = ctx->dir_uid_high;
            page->dir_uid_low = ctx->dir_uid_low;
        }

        dir_$release_wire(NAME_$HANDLE_TO_PTR(ctx->handle));
    }
}
