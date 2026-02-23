/*
 * MST_$ALLOC_TABLE_PAGE - Allocate a page table page for a segment
 *
 * Searches the MST page availability bitmap for a free page, marks it
 * as used, initializes it via mst_$init_table_page, and increments the
 * wired page count.
 *
 * The bitmap at MST_$PAGE_AVAIL_BITMAP has 12 words (32 bits each) = 384
 * possible page indices. A set bit means the page is available.
 *
 * Original address: 0x00E43F40
 * Size: 152 bytes
 */

#include "mst/mst_internal.h"

/* Page table page origin: page_index * 0x400 + this base = page address */
#define MST_PAGE_TABLE_ORIGIN  0xEF6000
#define MST_PAGE_SIZE          0x400
#define MST_PAGE_BITMAP_COUNT  12

/* Internal helper: init a page table page (allocate physical, install MMU, zero) */
extern void mst_$init_table_page(uint32_t page_addr);

status_$t MST_$ALLOC_TABLE_PAGE(uint16_t asid, uint16_t flags, uint16_t *table_ptr)
{
    uint16_t word_index;
    int16_t byte_offset;
    uint16_t bit_pos;
    uint32_t mask;
    uint16_t page_index;

    /* Skip if ASID is 0 or slot already populated */
    if (asid == 0 || *table_ptr != 0) {
        return status_$ok;
    }

    /* Search bitmap starting from hint for a word with free pages */
    word_index = MST_$PAGE_ALLOC_HINT;
    byte_offset = (int16_t)(MST_$PAGE_ALLOC_HINT << 2);

    while (MST_$PAGE_AVAIL_BITMAP[word_index] == 0) {
        if (word_index >= MST_PAGE_BITMAP_COUNT) {
            return status_$pmap_vm_resources_exhausted;
        }
        word_index++;
        byte_offset += 4;
    }

    /* Find first set bit in the word */
    bit_pos = 0;
    while ((MST_$PAGE_AVAIL_BITMAP[word_index] & (1u << (bit_pos & 0x1f))) == 0) {
        bit_pos++;
    }

    /* Update hint */
    MST_$PAGE_ALLOC_HINT = word_index;

    /* Clear bit to mark page as allocated */
    mask = ~(1u << (bit_pos & 0x1f));
    MST_$PAGE_AVAIL_BITMAP[word_index] &= mask;

    /* Compute page index: word * 32 + bit */
    page_index = (uint16_t)((int16_t)bit_pos + (uint16_t)(word_index * 32));
    *table_ptr = page_index;

    /* Initialize the page table page */
    mst_$init_table_page((uint32_t)page_index * MST_PAGE_SIZE + MST_PAGE_TABLE_ORIGIN);

    /* Track wired MST pages */
    MST_$MST_PAGES_WIRED++;

    return status_$ok;
}
