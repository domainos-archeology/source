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

void dir_$insert_entry(dir_insert_ctx_t *ctx, int16_t slot_idx,
                       uint32_t param_2, int16_t name_len,
                       status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
    int16_t local_base_offset;
    int16_t local_entry_size;

    *status_ret = status_$ok;

    /* Map the target page using B-tree path for this level */
    ctx->page_data = (uint8_t *)dir_$map_page(
        (void *)(uintptr_t)ctx->handle,
        ctx->path_page[slot_idx]);
    int16_t target_entry_idx = ctx->path_entry[slot_idx];

    /* Get page header info - check if this is the root page */
    int16_t page_link = *(int16_t *)(ctx->page_data + 10);

    if (page_link == 0) {
        /* Root page - base offset includes root header */
        ctx->inter_page = ctx->page_data;
        local_base_offset = *(int16_t *)(ctx->inter_page + 0x14) + 0x12;
    } else {
        local_base_offset = 0x12;
    }

    /* Compute number of entries on this page */
    int32_t diff = (int32_t)*(int16_t *)(ctx->page_data + 0x0E) -
                   (int32_t)local_base_offset;
    if (diff < 0) diff += 1;
    int16_t num_entries = (int16_t)(diff >> 1);

    /* Compute index base pointer */
    ctx->idx_base = ctx->page_data + local_base_offset;

    /* Compute required entry size based on page type */
    if ((*ctx->page_data >> 6) == 0) {
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

    /* Compute free space on page */
    ctx->free_space = *(int16_t *)(ctx->page_data + 0x10) -
                      *(int16_t *)(ctx->page_data + 0x0E);

    /* Try to reclaim space if needed and page has reclaimable bit set */
    if (ctx->free_space < aligned_size + 2 &&
        (*(uint16_t *)ctx->page_data & 0x2000) != 0) {
        DIR_$WIRE_PAGE((void *)(uintptr_t)ctx->handle, ctx->page_data);
        dir_$compact_page_entries(ctx);
        dir_$release_wire((void *)(uintptr_t)ctx->handle);
        ctx->free_space = *(int16_t *)(ctx->page_data + 0x10) -
                          *(int16_t *)(ctx->page_data + 0x0E);
    }

    if (ctx->free_space < aligned_size + 2) {
        /*
         * Page is full - need to split.
         * This is the complex path that handles B-tree page splitting.
         */
        int16_t split_threshold = 0x1F7;

        if (page_link == 0) {
            /* Root page split */
            if (ctx->max_depth == 8) {
                *status_ret = status_$directory_is_full;
                return;
            }
            ctx->inter_page = ctx->page_data;
            int16_t root_extra = *(int16_t *)(ctx->inter_page + 0x14);
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
                CRASH_SYSTEM((const status_$t *)&Naming_bad_request_header_ver_err);
            }
            ctx->temp_entry = ctx->page_data +
                (int32_t)*(int16_t *)(ctx->idx_base + idx_offset - 2);
            int16_t entry_sz = dir_$calc_entry_size(ctx->temp_entry);
            accum_size += entry_sz;
        } while (accum_size < split_threshold);

        if (page_link == 0) {
            /*
             * Root page split - allocate two new pages and make root an
             * internal node. This is the most complex code path.
             */
            dir_$alloc_split_page(ctx, 0xFF, status_ret);
            if (*status_ret != status_$ok) return;

            /* Allocate first new child page */
            ctx->page_count++;
            ctx->new_page = (uint8_t *)dir_$map_page(
                (void *)(uintptr_t)ctx->handle,
                ctx->split_pages[ctx->page_count]);

            /* Initialize new page header */
            *(uint16_t *)ctx->new_page = 0;
            /* Copy page type flags from root */
            *ctx->new_page = (*ctx->new_page & 0x3F) | (*ctx->page_data & 0xC0);
            /* Copy version byte */
            uint8_t ver_byte = *(ctx->page_data + 1) & 0x3F;
            *(ctx->new_page + 1) = (*(ctx->new_page + 1) & 0xC0) | ver_byte;
            /* Copy directory UID into page header */
            *(uint32_t *)(ctx->new_page + 2) = ctx->dir_uid_high;
            *(uint32_t *)(ctx->new_page + 6) = ctx->dir_uid_low;
            /* Set page link and sequence */
            *(uint16_t *)(ctx->new_page + 10) = ctx->split_pages[ctx->page_count];
            *(uint16_t *)(ctx->new_page + 0x0C) =
                *(uint16_t *)(ctx->new_page + 10) + 1;
            *(uint16_t *)(ctx->new_page + 0x0E) = 0x12;
            *(uint16_t *)(ctx->new_page + 0x10) = 0x400;

            DIR_$WIRE_PAGE((void *)(uintptr_t)ctx->handle, ctx->page_data);

            /* Determine starting entry for copy based on page type */
            int16_t copy_start;
            if ((*ctx->page_data >> 6) == 0) {
                copy_start = 2;  /* Leaf page: skip first 2 entries */
            } else {
                copy_start = 1;  /* Internal page: skip 1 entry */
            }

            int16_t move_from;
            if (found_insert_point < 0) {
                /* Insert point is before split point */
                dir_$move_entries_to_page(ctx, copy_start, target_entry_idx);
                if ((*ctx->page_data >> 6) == 0) {
                    copy_start = target_entry_idx;
                } else {
                    copy_start = target_entry_idx + 1;
                }
                dir_$write_entry_to_page(ctx, 0xFF, &ctx->new_page, copy_start);
                move_from = target_entry_idx + 1;
            } else {
                move_from = copy_start;
            }
            dir_$move_entries_to_page(ctx, move_from, split_entry);

            /* Allocate second new child page */
            ctx->page_count++;
            ctx->new_page = (uint8_t *)dir_$map_page(
                (void *)(uintptr_t)ctx->handle,
                ctx->split_pages[ctx->page_count]);

            /* Initialize second page header */
            *(uint16_t *)ctx->new_page = 0;
            *ctx->new_page = (*ctx->new_page & 0x3F) | (*ctx->page_data & 0xC0);
            ver_byte = *(ctx->page_data + 1) & 0x3F;
            *(ctx->new_page + 1) = (*(ctx->new_page + 1) & 0xC0) | ver_byte;
            *(uint32_t *)(ctx->new_page + 2) = ctx->dir_uid_high;
            *(uint32_t *)(ctx->new_page + 6) = ctx->dir_uid_low;
            *(uint16_t *)(ctx->new_page + 10) = ctx->split_pages[ctx->page_count];
            *(uint16_t *)(ctx->new_page + 0x0C) = 0xFFFF;  /* Last page marker */
            *(uint16_t *)(ctx->new_page + 0x0E) = 0x12;
            *(uint16_t *)(ctx->new_page + 0x10) = 0x400;

            if (found_insert_point < 0) {
                move_from = split_entry + 1;
            } else {
                /* Insert point is after split point */
                dir_$move_entries_to_page(ctx, split_entry + 1, target_entry_idx);
                move_from = target_entry_idx + 1;
                dir_$write_entry_to_page(ctx, 0, &ctx->new_page,
                                         move_from - split_entry);
            }
            dir_$move_entries_to_page(ctx, move_from, num_entries);

            /* Convert root page to internal node */
            *(uint16_t *)ctx->page_data = 0;
            *(ctx->page_data + 1) = (*(ctx->page_data + 1) & 0xC0) | 5;
            *ctx->page_data = (*ctx->page_data & 0x3F) | 0x40;
            *(int16_t *)(ctx->page_data + 0x0E) = local_base_offset + 4;
            *(uint16_t *)(ctx->page_data + 0x10) = 0x400;
            *(uint16_t *)(ctx->page_data + 0x10) =
                (*(int16_t *)(ctx->page_data + 0x10) - 6) & 0xFFFC;

            /* Create first internal entry (pointer to first child) */
            *(uint16_t *)(ctx->idx_base) =
                *(uint16_t *)(ctx->page_data + 0x10);
            ctx->temp_entry = ctx->page_data +
                (int32_t)*(int16_t *)(ctx->page_data + 0x10);
            *(uint16_t *)ctx->temp_entry = 0;
            *ctx->temp_entry = (*ctx->temp_entry & 0xF8) | 1;
            *(ctx->temp_entry + 1) = 1;
            /* Child page is the previous split page (page_count - 1) */
            *(uint16_t *)(ctx->temp_entry + 2) =
                ctx->split_pages[ctx->page_count - 1];
            *(ctx->temp_entry + 4) = 0;

            /* Build second internal entry using first entry of first child */
            uint8_t *first_child_idx = ctx->new_page + 0x12;
            uint8_t *first_child_entry = ctx->new_page +
                (int32_t)*(int16_t *)first_child_idx;
            uint8_t *first_child_name = first_child_entry +
                DIR_$NAME_OFFSET_TABLE[*first_child_entry & 7];

            /* Allocate space for second internal entry */
            *(uint16_t *)(ctx->page_data + 0x10) =
                (*(int16_t *)(ctx->page_data + 0x10) - 4 -
                 (uint16_t)*(first_child_entry + 1)) & 0xFFFC;

            *(uint16_t *)(ctx->idx_base + 2) =
                *(uint16_t *)(ctx->page_data + 0x10);
            ctx->temp_entry = ctx->page_data +
                (int32_t)*(int16_t *)(ctx->page_data + 0x10);
            *(uint16_t *)ctx->temp_entry = 0;
            *ctx->temp_entry = (*ctx->temp_entry & 0xF8) | 1;
            *(ctx->temp_entry + 1) = *(first_child_entry + 1);
            /* Child page is the current split page */
            *(uint16_t *)(ctx->temp_entry + 2) =
                ctx->split_pages[ctx->page_count];

            /* Copy name from first entry of second child */
            {
                uint16_t nlen = *(ctx->temp_entry + 1) - 1;
                if ((int32_t)((uint32_t)nlen << 16) >= 0) {
                    int16_t j = 1;
                    do {
                        *(ctx->temp_entry + 3 + j) = first_child_name[j - 1];
                        j++;
                        nlen--;
                    } while (nlen != 0xFFFF);
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
                    CRASH_SYSTEM((const status_$t *)&Naming_bad_request_header_ver_err);
                }
                ctx->temp_entry = ctx->page_data +
                    (int32_t)*(int16_t *)(ctx->idx_base + split_entry * 2);
                recursive_param =
                    (int32_t)DIR_$NAME_OFFSET_TABLE[*ctx->temp_entry & 7] +
                    (uint32_t)*(uint16_t *)(ctx->page_data + 10) * 0x400 +
                    (int32_t)*(int16_t *)(ctx->idx_base + split_entry * 2);
                recursive_name_len = (uint16_t)*(ctx->temp_entry + 1);
            }

            /* Recurse up one level */
            dir_$insert_entry(ctx, slot_idx - 1, recursive_param,
                              recursive_name_len, status_ret);
            if (*status_ret != status_$ok) return;

            /* Re-map the page after recursion (may have been invalidated) */
            ctx->page_data = (uint8_t *)dir_$map_page(
                (void *)(uintptr_t)ctx->handle,
                ctx->path_page[slot_idx]);
            ctx->idx_base = ctx->page_data + local_base_offset;

            /* Allocate new page for the split */
            ctx->page_count++;
            ctx->new_page = (uint8_t *)dir_$map_page(
                (void *)(uintptr_t)ctx->handle,
                ctx->split_pages[ctx->page_count]);

            /* Initialize new page from current page */
            *(uint16_t *)ctx->new_page = *(uint16_t *)ctx->page_data;
            *(uint32_t *)(ctx->new_page + 2) = ctx->dir_uid_high;
            *(uint32_t *)(ctx->new_page + 6) = ctx->dir_uid_low;
            *(uint16_t *)(ctx->new_page + 10) = ctx->split_pages[ctx->page_count];
            *(uint16_t *)(ctx->new_page + 0x0C) =
                *(uint16_t *)(ctx->page_data + 0x0C);
            *(uint16_t *)(ctx->new_page + 0x0E) = 0x12;
            *(uint16_t *)(ctx->new_page + 0x10) = 0x400;

            DIR_$WIRE_PAGE((void *)(uintptr_t)ctx->handle, ctx->page_data);

            int16_t move_from;
            if (found_insert_point >= 0) {
                /* Insert point is after split point */
                dir_$move_entries_to_page(ctx, split_entry + 1, target_entry_idx);
                move_from = target_entry_idx + 1;
                dir_$write_entry_to_page(ctx, 0, &ctx->new_page,
                                         move_from - split_entry);
            } else {
                move_from = split_entry + 1;
            }
            dir_$move_entries_to_page(ctx, move_from, num_entries);

            /* Update current page entry count */
            *(int16_t *)(ctx->page_data + 0x0E) =
                local_base_offset + split_entry * 2;
            num_entries = split_entry;

            dir_$compact_page_entries(ctx);

            /* If insert point was before split point, insert here */
            if (found_insert_point < 0) {
                int16_t new_count = split_entry + 1;
                /* Shift index entries to make room */
                uint16_t shift_count = new_count - (target_entry_idx + 2);
                if ((int32_t)((uint32_t)shift_count << 16) >= 0) {
                    int16_t si = new_count * 2;
                    int32_t di = new_count * 2;
                    do {
                        *(int16_t *)(ctx->idx_base + di - 2) =
                            *(int16_t *)(ctx->idx_base + si - 4);
                        di -= 2;
                        si -= 2;
                        shift_count--;
                    } while (shift_count != 0xFFFF);
                }
                dir_$write_entry_to_page(ctx, 0, &ctx->page_data,
                                         target_entry_idx + 1);
            }

            /* Update page's next-page pointer */
            *(uint16_t *)(ctx->page_data + 0x0C) =
                ctx->split_pages[ctx->page_count];
        }

        /* Update directory UID in page header */
        *(uint32_t *)(ctx->page_data + 2) = ctx->dir_uid_high;
        *(uint32_t *)(ctx->page_data + 6) = ctx->dir_uid_low;

        dir_$release_wire((void *)(uintptr_t)ctx->handle);

        /* If we've reached the original slot level, finalize */
        if (ctx->max_depth == slot_idx) {
            dir_$finalize_split(ctx, status_ret);
        }
    } else {
        /*
         * Simple case - enough space on the page for the new entry.
         * Wire the page, shift index entries, and write the entry.
         */
        if ((*ctx->page_data >> 6) == 1) {
            /* Internal page - may need to allocate split page first */
            dir_$alloc_split_page(ctx, 0, status_ret);
            if (*status_ret != status_$ok) return;
        }

        DIR_$WIRE_PAGE((void *)(uintptr_t)ctx->handle, ctx->page_data);

        /* Shift index entries from target_entry_idx+1 to num_entries */
        num_entries++;
        {
            uint16_t shift_count = num_entries - (target_entry_idx + 2);
            if ((int32_t)((uint32_t)shift_count << 16) >= 0) {
                int16_t si = num_entries * 2;
                int32_t di = num_entries * 2;
                do {
                    *(int16_t *)(ctx->idx_base + di - 2) =
                        *(int16_t *)(ctx->idx_base + si - 4);
                    di -= 2;
                    si -= 2;
                    shift_count--;
                } while (shift_count != 0xFFFF);
            }
        }

        /* Handle FIM cleanup for type 4 entries on non-volatile dirs */
        int32_t fim_status = 0;
        if (*(int8_t *)((char *)(uintptr_t)ctx->handle + 0x0E) >= 0 &&
            ctx->entry_type == 4) {
            fim_status = FIM_$CLEANUP(ctx->fim_data);
        }

        if (fim_status == 0 || fim_status == 0x120035) {
            dir_$write_entry_to_page(ctx, 0, &ctx->page_data,
                                     target_entry_idx + 1);
            if (fim_status == 0x120035) {
                FIM_$RLS_CLEANUP(ctx->fim_data);
            }
        } else {
            dir_$release_wire((void *)(uintptr_t)ctx->handle);
            dir_$remove_entry((void *)(uintptr_t)ctx->handle,
                              ctx->name, ctx->name_len, 4,
                              ctx->remove_uid, status_ret);
            FIM_$SIGNAL(fim_status);
        }

        /* Update directory UID in page header for volatile directories */
        if (*(int8_t *)((char *)(uintptr_t)ctx->handle + 0x0E) < 0) {
            *(uint32_t *)(ctx->page_data + 2) = ctx->dir_uid_high;
            *(uint32_t *)(ctx->page_data + 6) = ctx->dir_uid_low;
        }

        dir_$release_wire((void *)(uintptr_t)ctx->handle);
    }
}
