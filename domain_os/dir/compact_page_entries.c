/*
 * dir_$compact_page_entries - Compact/reclaim dead entry space on a directory page
 *
 * Originally a nested Pascal subprocedure of dir_$insert_entry (which is itself
 * nested within dir_$add_entry). This function scans all entries on a page and
 * reclaims space occupied by dead entries (those with bit 7 set in byte 0,
 * as marked by dir_$move_entries_to_page).
 *
 * For each dead entry found:
 *   1. Shifts all entries between free_space_start and the dead entry to higher
 *      addresses by the dead entry's size, overwriting the dead entry.
 *   2. Updates index table entries that referenced shifted entries (adds the
 *      dead entry's size to their offsets).
 *   3. Advances the free_space_start (page offset 0x10) by the dead entry's size,
 *      reclaiming that space for new allocations.
 *
 * After processing all entries, the reclaimable-entries flag (bit 5 in byte 0)
 * is cleared.
 *
 * The function accesses the insert_entry parent's num_entries count. In the
 * original M68K code, this was read from insert_entry's frame at offset -0x0E
 * via the Pascal static link chain. In the C flattening, we compute it from the
 * page header (page_data[0x0E] - idx_base_offset) / 2, which produces the same
 * value at both call sites.
 *
 * Page header layout (relevant fields):
 *   offset 0x00: flags byte (bit 5 = 0x20 = reclaimable entries present)
 *   offset 0x0E: end-of-index-table offset (grows upward by 2 per entry)
 *   offset 0x10: start-of-free-space offset (grows downward by entry size)
 *
 * Entry layout:
 *   byte 0: flags (bit 7 = 0x80 = dead entry marker)
 *   byte 1: name length
 *
 * Assembly correspondence:
 *   btst.l #0xd,D0  → byte 0 bit 5 check (0x20) on big-endian word
 *   bclr.b #0x5,(A0) → byte 0 bit 5 clear
 *   tst.w (A0); bpl  → byte 0 bit 7 check (0x80) on big-endian word
 *
 * Parameters:
 *   ctx - Shared insertion context (contains page_data, idx_base)
 *
 * Original address: 0x00E4F2DA
 * Original size: 224 bytes
 */

#include "dir/dir_internal.h"

void dir_$compact_page_entries(dir_insert_ctx_t *ctx)
{
    uint8_t *page_data = ctx->page_data;

    /* Check if page has reclaimable entries.
     * M68K: move.w (A0),D0; btst.l #0xd,D0 — on big-endian, bit 13 of the
     * word is bit 5 of byte 0. Use byte access for portability. */
    if ((*page_data & 0x20) == 0) {
        return;
    }

    /* Clear the reclaimable-entries flag (bclr.b #0x5,(A0)) */
    *page_data &= ~0x20;

    /* Compute number of entries in the index table.
     * In the original code, this was read from insert_entry's frame (A6-0x0E).
     * We compute the equivalent from the page header. */
    int16_t idx_start_offset = (int16_t)(ctx->idx_base - ctx->page_data);
    int32_t diff = (int32_t)*(int16_t *)(ctx->page_data + 0x0E) -
                   (int32_t)idx_start_offset;
    if (diff < 0) diff += 1;
    int16_t num_entries = (int16_t)(diff >> 1);

    /* Scan entries from free_space_start to end of page (0x400).
     * D6 in the assembly is the scan pointer. */
    page_data = ctx->page_data;
    int16_t free_start_off = *(int16_t *)(page_data + 0x10);
    uint8_t *scan_ptr = page_data + (int32_t)free_start_off;

    while (scan_ptr < page_data + 0x400) {
        /* Calculate this entry's aligned size */
        int16_t entry_size = (int16_t)dir_$calc_entry_size(scan_ptr);

        /* Check if entry is dead (bit 7 set in byte 0).
         * M68K: tst.w (A0); bpl — on big-endian, the sign bit of the word
         * is bit 7 of byte 0. Use byte access for portability. */
        if (*scan_ptr & 0x80) {
            /* Dead entry found - compact it out.
             *
             * Calculate number of words between entries_start and this dead entry.
             * entries_start = page_data + current free_space_start offset. */
            page_data = ctx->page_data;
            int16_t current_free_off = *(int16_t *)(page_data + 0x10);
            uint8_t *entries_start = page_data + (int32_t)current_free_off;

            int32_t byte_diff = (int32_t)(scan_ptr - entries_start);
            if (byte_diff < 0) byte_diff += 1;
            int16_t word_count = (int16_t)(byte_diff >> 1);

            /* Shift entries to higher addresses by entry_size, working backward.
             * This overwrites the dead entry with the tail of preceding entries
             * and opens up space at entries_start. */
            int16_t loop_counter = word_count - 1;
            if (loop_counter >= 0) {
                /* Compute entry_size in words (for destination offset) */
                int16_t size_words = entry_size;
                if (size_words < 0) size_words += 1;
                size_words >>= 1;

                int16_t wi = word_count;  /* Current word index (from entries_start) */
                do {
                    /* Copy word from [entries_start + (wi-1)*2]
                     *            to  [entries_start + (wi-1+size_words)*2]
                     * Assembly: move.w -(A0),(-0x2,A1,D0w*0x1) */
                    int32_t src_byte_off = (int32_t)(int16_t)wi;
                    src_byte_off += src_byte_off;  /* *2 */
                    uint16_t *src = (uint16_t *)(entries_start + src_byte_off);
                    src--;  /* pre-decrement (move.w -(A0),...) */

                    int16_t dst_idx = wi + size_words;
                    *(uint16_t *)((uint8_t *)entries_start +
                                  (int16_t)(dst_idx * 2) - 2) = *src;

                    wi--;
                    loop_counter--;
                } while (loop_counter != -1);
            }

            /* Update index table entries.
             * Entries pointing to offsets below the dead entry were shifted
             * and need their offsets increased by entry_size.
             * Assembly: cmp.w (-0x2,A3,D0*0x1),D3w; ble skip */
            int16_t dead_page_off = (int16_t)((uint8_t *)scan_ptr - ctx->page_data);
            int16_t idx_loop = num_entries - 1;
            if (idx_loop >= 0) {
                int32_t idx_byte = 2;
                do {
                    int16_t idx_val = *(int16_t *)(ctx->idx_base + idx_byte - 2);
                    if (dead_page_off > idx_val) {
                        /* Entry was in shifted region - adjust offset */
                        int16_t new_val = entry_size + idx_val;
                        *(int16_t *)(ctx->idx_base + idx_byte - 2) = new_val;
                    }
                    idx_byte += 2;
                    idx_loop--;
                } while (idx_loop != -1);
            }

            /* Reclaim space: advance free_space_start by entry_size */
            *(int16_t *)(ctx->page_data + 0x10) += entry_size;
        }

        /* Advance scan pointer to next entry */
        scan_ptr += (int32_t)(int16_t)entry_size;
    }
}
