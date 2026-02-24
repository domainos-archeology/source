/*
 * dir_$write_entry_to_page - Write/insert entry data into a directory page
 *
 * Originally a nested Pascal subprocedure of dir_$insert_entry within
 * dir_$add_entry. This function writes a new directory entry to a page,
 * including the type-specific header, UID/link data, and entry name.
 *
 * For leaf pages (page_type == 0):
 *   Type 1: Invalid for leaf pages (crashes)
 *   Type 2: File reference (8-byte UID, 4 bytes reserved)
 *   Type 3: Hard link (8-byte UID, 4-byte extra_val, 4 bytes reserved)
 *   Type 4: Soft link (2-byte link_len, 2-byte overflow_page, 6 bytes reserved)
 *
 * For internal pages (page_type != 0):
 *   Always type 1 with a 2-byte child page pointer
 *
 * After writing the type-specific header, copies the entry name either from
 * ctx->name (when src_name_loc == 0) or from another page via cross-page
 * copy (when src_name_loc != 0, encoding source as page_num << 10 | offset).
 *
 * For type 4 entries with inline link data (overflow_page == -1), also copies
 * link data from ctx->link_data after the name.
 *
 * Page header layout (relevant fields):
 *   offset 0x0A: page_link (0 = root page)
 *   offset 0x0E: end-of-index-table offset
 *   offset 0x10: start-of-free-space offset (decremented by aligned_size)
 *   offset 0x14: root page header extra size (only on root pages)
 *
 * Parameters:
 *   ctx          - Shared insertion context (parent frame variables)
 *   flag         - If negative (0xFF), use split_pages[page_count+2] for
 *                  internal page child pointer; if >= 0, use [page_count+1]
 *   page_ptr_ref - Pointer to page data pointer (e.g., &ctx->new_page)
 *   count        - 1-based entry position in the index table
 *   name_len     - Entry name length (from insert_entry's parameter)
 *   aligned_size - Aligned total entry size (from insert_entry's computation)
 *   src_name_loc - Source page/offset for name copy (0 = use ctx->name,
 *                  non-zero = (page_num << 10) | offset_within_page)
 *
 * Original address: 0x00E4F100
 * Original size: 384 bytes
 */

#include "dir/dir_internal.h"

void dir_$write_entry_to_page(dir_insert_ctx_t *ctx, uint8_t flag,
                              uint8_t **page_ptr_ref, int16_t count,
                              int16_t name_len, uint16_t aligned_size,
                              uint32_t src_name_loc)
{
    uint8_t *page_data = *page_ptr_ref;

    /* Reserve space for the entry by moving free space pointer down */
    *(int16_t *)(page_data + 0x10) -= (int16_t)aligned_size;

    /* Compute pointer to index table entry for this position.
     * Index entries are 2-byte offsets starting at page+0x12.
     * Entry positions are 1-based (Pascal convention). */
    int16_t *idx_entry = (int16_t *)(page_data + (int32_t)(count - 1) * 2 + 0x12);

    /* For root pages (page_link == 0), adjust by root header extra offset */
    if (*(int16_t *)(page_data + 0x0A) == 0) {
        idx_entry = (int16_t *)((uint8_t *)idx_entry +
                                (int32_t)*(int16_t *)(page_data + 0x14));
    }

    /* Re-read page data pointer (may alias through page_ptr_ref) */
    page_data = *page_ptr_ref;

    /* Set this index entry to point to the reserved free space */
    *idx_entry = *(int16_t *)(page_data + 0x10);

    /* Grow the index table by one entry (2 bytes) */
    *(int16_t *)(page_data + 0x0E) += 2;

    /* Get pointer to the entry data area */
    uint8_t *entry = page_data + (int32_t)*idx_entry;

    /* Initialize first 2 bytes: clear type/flags and name_len */
    *(uint16_t *)entry = 0;

    /* Set name length byte (byte 1 of entry) */
    entry[1] = (uint8_t)name_len;

    /* Determine page type from top 2 bits of first byte */
    uint8_t page_type = *page_data >> 6;

    if (page_type != 0) {
        /*
         * INTERNAL PAGE - always type 1 with child page pointer.
         * The child page number comes from the split_pages array.
         */
        entry[0] = (entry[0] & 0xF8) | 1;

        if ((int8_t)flag < 0) {
            /* Flag negative (0xFF): child is next split page (page_count + 2) */
            *(uint16_t *)(entry + 2) = ctx->split_pages[ctx->page_count + 2];
        } else {
            /* Flag non-negative: child is current split page (page_count + 1) */
            *(uint16_t *)(entry + 2) = ctx->split_pages[ctx->page_count + 1];
        }
        goto name_copy;
    }

    /* LEAF PAGE - set entry type from context */
    entry[0] = (entry[0] & 0xF8) | (uint8_t)(ctx->entry_type & 7);

    switch (entry[0] & 7) {
    case 1:
        /* Type 1 is invalid for leaf pages */
        CRASH_SYSTEM((const status_$t *)&Naming_bad_request_header_ver_err);
        break;

    case 2: {
        /* Type 2 - File reference: 8-byte UID + 4 bytes cleared */
        uint32_t *uid_ptr = (uint32_t *)ctx->uid;
        *(uint32_t *)(entry + 4) = uid_ptr[0];
        *(uint32_t *)(entry + 8) = uid_ptr[1];
        *(uint32_t *)(entry + 0x0C) = 0;
        /* Fall through to clear bytes 2-3 */
        entry[2] = 0;
        entry[3] = 0;
        break;
    }

    case 3: {
        /* Type 3 - Hard link: 8-byte UID + 4-byte extra + 4 bytes cleared */
        uint32_t *uid_ptr = (uint32_t *)ctx->uid;
        *(uint32_t *)(entry + 4) = uid_ptr[0];
        *(uint32_t *)(entry + 8) = uid_ptr[1];
        *(uint32_t *)(entry + 0x0C) = ctx->extra_val;
        *(uint32_t *)(entry + 0x10) = 0;
        entry[2] = 0;
        entry[3] = 0;
        break;
    }

    case 4:
        /* Type 4 - Soft link: link_len + overflow_page + 6 bytes cleared */
        *(uint16_t *)(entry + 2) = (uint16_t)ctx->link_len;
        *(int16_t *)(entry + 4) = ctx->overflow_page;
        *(uint32_t *)(entry + 6) = 0;
        *(uint16_t *)(entry + 0x0A) = 0;
        break;

    default:
        /* Types 0, 5-7: skip type-specific setup */
        break;
    }

name_copy:
    /* Common path: copy entry name to appropriate offset */
    {
        int16_t name_offset = DIR_$NAME_OFFSET_TABLE[entry[0] & 7];
        uint8_t *name_dest = entry + (int32_t)name_offset;

        if (src_name_loc == 0) {
            /* Direct copy from ctx->name */
            uint16_t remaining = (uint16_t)(name_len - 1);
            if ((int32_t)((uint32_t)remaining << 16) >= 0) {
                int16_t i = 1;
                uint8_t *src = (uint8_t *)ctx->name;
                do {
                    name_dest[i - 1] = src[i - 1];
                    i++;
                    remaining--;
                } while (remaining != 0xFFFF);
            }
        } else {
            /* Cross-page copy: name is on another page encoded in src_name_loc */
            dir_$copy_name_cross_page(
                ctx->handle,
                (int16_t)(src_name_loc >> 10),        /* source page number */
                (int16_t)(src_name_loc & 0x3FF),      /* source offset */
                *(int16_t *)(page_data + 0x0A),       /* dest page link */
                (int16_t)(name_dest - page_data), /* dest offset */
                name_len);
        }
    }

    /* For type 4 leaf entries with inline link data, copy it after the name */
    page_data = *page_ptr_ref;
    if ((*page_data >> 6) == 0 &&
        ctx->entry_type == 4 &&
        ctx->overflow_page == -1) {

        int16_t entry_start = *(int16_t *)(page_data + 0x10);
        /* Link data goes at entry + 0x0C + name_len (hardcoded offset per assembly) */
        uint8_t *link_dest = page_data + (int32_t)entry_start + 0x0C +
                             (int32_t)name_len;

        uint16_t link_remaining = (uint16_t)(ctx->link_len - 1);
        if ((int32_t)((uint32_t)link_remaining << 16) >= 0) {
            int16_t j = 1;
            uint8_t *link_src = (uint8_t *)ctx->link_data;
            do {
                link_dest[j - 1] = link_src[j - 1];
                j++;
                link_remaining--;
            } while (link_remaining != 0xFFFF);
        }
    }
}
