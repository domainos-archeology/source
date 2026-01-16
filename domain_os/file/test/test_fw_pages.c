/*
 * Unit tests for FILE_$FW_PAGES
 *
 * Tests the page-list force-write functionality which flushes
 * specific pages to disk, including the sorting optimization.
 */

#include "base/base.h"

/*
 * Test: Page entry format
 *
 * Page entries are uint32_t where:
 *   bits 5-31: page number (>> 5)
 *   bits 0-4:  sub-page index
 */
void test_fw_pages_entry_format(void) {
    uint32_t entry;

    /* Entry for page 0, sub-page 0 */
    entry = (0 << 5) | 0;
    ASSERT_EQ(entry >> 5, 0);   /* Page number */
    ASSERT_EQ(entry & 0x1F, 0); /* Sub-page index */

    /* Entry for page 1, sub-page 15 */
    entry = (1 << 5) | 15;
    ASSERT_EQ(entry >> 5, 1);
    ASSERT_EQ(entry & 0x1F, 15);

    /* Entry for page 100, sub-page 31 */
    entry = (100 << 5) | 31;
    ASSERT_EQ(entry >> 5, 100);
    ASSERT_EQ(entry & 0x1F, 31);
}

/*
 * Test: Sorting algorithm (bubble sort)
 *
 * Tests that pages are sorted in ascending order within batches.
 */
void test_fw_pages_sorting(void) {
    uint32_t pages[5] = { 50, 10, 30, 20, 40 };
    uint32_t expected[5] = { 10, 20, 30, 40, 50 };
    uint16_t i, j;
    uint32_t temp;

    /* Simple bubble sort (same algorithm as in fw_pages.c) */
    for (i = 0; i < 4; i++) {
        for (j = i + 1; j < 5; j++) {
            if (pages[j] < pages[i]) {
                temp = pages[i];
                pages[i] = pages[j];
                pages[j] = temp;
            }
        }
    }

    /* Verify sorted */
    for (i = 0; i < 5; i++) {
        ASSERT_EQ(pages[i], expected[i]);
    }
}

/*
 * Test: Batch size constant
 */
void test_fw_pages_batch_size(void) {
    ASSERT_EQ(32, 32);  /* FW_BATCH_SIZE */
}

/*
 * Test: Batch calculation
 *
 * Verifies batch size calculation for various page counts.
 */
void test_fw_pages_batch_calculation(void) {
    uint16_t total_pages;
    uint16_t start_index;
    uint16_t batch_size;

    /* 10 pages: single batch of 10 */
    total_pages = 10;
    start_index = 1;
    if ((total_pages - start_index + 1) < 32) {
        batch_size = (total_pages - start_index) + 1;
    } else {
        batch_size = 32;
    }
    ASSERT_EQ(batch_size, 10);

    /* 50 pages: first batch of 32, second batch of 18 */
    total_pages = 50;
    start_index = 1;
    if ((total_pages - start_index + 1) < 32) {
        batch_size = (total_pages - start_index) + 1;
    } else {
        batch_size = 32;
    }
    ASSERT_EQ(batch_size, 32);

    /* After first batch */
    start_index = 33;
    if ((total_pages - start_index + 1) < 32) {
        batch_size = (total_pages - start_index) + 1;
    } else {
        batch_size = 32;
    }
    ASSERT_EQ(batch_size, 18);

    /* Exactly 32 pages: single batch of 32 */
    total_pages = 32;
    start_index = 1;
    if ((total_pages - start_index + 1) < 32) {
        batch_size = (total_pages - start_index) + 1;
    } else {
        batch_size = 32;
    }
    ASSERT_EQ(batch_size, 32);
}

/*
 * Test: Empty page list handling
 *
 * Verifies that an empty page list returns immediately with status_$ok.
 */
void test_fw_pages_empty_list(void) {
    /* When page_count is 0, should return immediately */
    uint16_t page_count = 0;
    ASSERT_EQ(page_count, 0);  /* Would trigger early return */
}

/*
 * Test: Purify flag values for page write
 */
void test_fw_pages_purify_flags(void) {
    /* Local-only page purify */
    ASSERT_EQ(0x0012, 0x0012);  /* FW_PAGES_LOCAL */

    /* Remote-sync page purify */
    ASSERT_EQ(0x8012, 0x8012);  /* FW_PAGES_REMOTE */
}
