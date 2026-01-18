/*
 * MEM_$PARITY_LOG - Log a memory parity error
 *
 * Records a parity error in the memory parity tracking table.
 *
 * Original address: 0x00E0ADB0
 * Size: 182 bytes
 */

#include "mem/mem.h"
#include "parity/parity_internal.h"

/*
 * MEM_$PARITY_LOG
 *
 * The tracking table maintains:
 * 1. Per-board error counts (board 1 for addresses < 3MB, board 2 for >= 3MB)
 * 2. Per-page records for tracking which specific pages have errors
 *
 * When a page has an error, we first check if it's already in the table.
 * If so, we increment its count. If not, we replace the entry with the
 * lowest count with this new page.
 *
 * Parameters:
 *   phys_addr - Physical address where parity error occurred
 */
void MEM_$PARITY_LOG(uint32_t phys_addr)
{
    int16_t board_index;
    int16_t search_count;
    int16_t record_index;
    int16_t min_count_index;
    int16_t min_count;
    int16_t scan_index;
    uint8_t page_id;           /* Bits 8-13 of address (64-page region identifier) */
    mem_parity_record_t *rec;

    /*
     * Determine which memory board the error is on.
     * Board 1: addresses < 3MB (0x300000)
     * Board 2: addresses >= 3MB
     */
    if (phys_addr < MEM_BOARD_BOUNDARY) {
        board_index = 1;
    } else {
        board_index = 2;
    }

    /* Increment the per-board error count */
    if (board_index == 1) {
        MEM_BOARD1_COUNT++;
    } else {
        MEM_BOARD2_COUNT++;
    }

    /*
     * Extract page identifier from address.
     * This is bits 8-13 (6 bits), giving a 64-page region identifier.
     * This allows grouping nearby pages together for tracking.
     */
    page_id = (phys_addr >> 8) & 0x3F;

    /*
     * Search existing records for a matching page.
     * We compare the page ID from the stored address with the new one.
     */
    search_count = MEM_PARITY_PAGE_RECORDS - 1;  /* 3 iterations (0-3 records, but skip first?) */
    record_index = 1;
    rec = &MEM_PARITY_RECORDS[0];

    while (search_count >= 0) {
        /* Check if this record is in use (count > 0) */
        if (rec->count == 0) {
            break;  /* Found empty slot */
        }

        /* Compare page IDs */
        if (((rec->phys_addr >> 8) & 0x3F) == page_id) {
            /* Match found - increment this record's count */
            rec->count++;
            return;
        }

        record_index++;
        rec++;
        search_count--;
    }

    /*
     * No matching record found. Find the record with the lowest count
     * and replace it with this new error.
     */
    min_count_index = 1;
    min_count = MEM_PARITY_RECORDS[0].count;
    scan_index = 2;
    rec = &MEM_PARITY_RECORDS[1];  /* Start from second record */

    for (search_count = 2; search_count >= 0; search_count--) {
        if ((int16_t)rec->count < min_count) {
            min_count = rec->count;
            min_count_index = scan_index;
        }
        scan_index++;
        rec++;
    }

    /*
     * Replace the record with minimum count.
     * Calculate the record offset: index * 0x12 (18 bytes per record).
     */
    rec = &MEM_PARITY_RECORDS[min_count_index - 1];
    rec->count = 1;
    rec->phys_addr = phys_addr;
}
