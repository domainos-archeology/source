/*
 * dir_$move_entries_to_page - Move entries from source page to new page during B-tree split
 *
 * Originally a nested Pascal subprocedure of dir_$insert_entry. Copies directory
 * entries from the current source page (ctx->page_data) to the destination page
 * (ctx->new_page) during a B-tree page split. Each copied entry is marked as dead
 * (bit 7 set) on the source page. The source page's reclaimable-entries bit (0x20)
 * is also set to indicate dead entries need compaction.
 *
 * Entry indices are 1-based (Pascal convention). Both from_idx and to_idx are
 * inclusive - entries from_idx through to_idx are moved.
 *
 * Page header layout (relevant fields):
 *   offset 0x00: flags byte (bit 5 = reclaimable entries, bit 7 = dead entry)
 *   offset 0x0E: end-of-index-table offset (grows upward by 2 per entry)
 *   offset 0x10: start-of-free-space offset (grows downward by entry size)
 *
 * Parameters:
 *   ctx      - Shared insertion context (contains page_data, idx_base, new_page)
 *   from_idx - First entry to move (1-based, inclusive)
 *   to_idx   - Last entry to move (1-based, inclusive)
 *
 * Original address: 0x00E4EF7C
 * Original size: 184 bytes
 */

#include "dir/dir_internal.h"

void dir_$move_entries_to_page(dir_insert_ctx_t *ctx,
                               int16_t from_idx, int16_t to_idx)
{
    if (from_idx > to_idx) {
        return;
    }

    /* Set reclaimable-entries flag on source page - dead entries will need compaction */
    *ctx->page_data |= 0x20;

    int16_t count = to_idx - from_idx;
    int32_t idx_offset = (int32_t)from_idx * 2;

    do {
        /* Get source entry pointer from index table
         * 1-based indexing: entry N is at idx_base[(N-1)*2] */
        int16_t src_off = *(int16_t *)(ctx->idx_base + idx_offset - 2);
        uint8_t *src_entry = ctx->page_data + (int32_t)src_off;

        /* Compute aligned entry size */
        int16_t entry_size = (int16_t)dir_$calc_entry_size(src_entry);

        /* Get next free index slot on destination page */
        int16_t *dst_idx_slot = (int16_t *)(ctx->new_page +
            (int32_t)*(int16_t *)(ctx->new_page + 0x0E));

        /* Update destination page header: grow index table, shrink free space */
        *(int16_t *)(ctx->new_page + 0x0E) += 2;
        *(int16_t *)(ctx->new_page + 0x10) -= entry_size;

        /* Write the entry data offset into the index slot */
        *dst_idx_slot = *(int16_t *)(ctx->new_page + 0x10);

        /* Get destination entry pointer */
        uint16_t *dst_words = (uint16_t *)(ctx->new_page + (int32_t)*dst_idx_slot);

        /* Copy entry data word-by-word.
         * The entry size is always 4-byte aligned, so this is safe. */
        int16_t word_count = entry_size;
        if (word_count < 0) {
            word_count += 1;
        }
        word_count = (word_count >> 1) - 1;

        if (word_count >= 0) {
            uint16_t *src_words = (uint16_t *)src_entry;
            int16_t w = word_count;
            do {
                *dst_words = *src_words;
                dst_words++;
                src_words++;
                w--;
            } while (w != -1);
        }

        /* Mark source entry as dead */
        *src_entry |= 0x80;

        idx_offset += 2;
        count--;
    } while (count != -1);
}
