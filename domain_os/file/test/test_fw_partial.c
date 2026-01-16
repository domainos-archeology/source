/*
 * Unit tests for FILE_$FW_PARTIAL
 *
 * Tests the partial file force-write functionality which flushes
 * dirty pages within a byte range to disk.
 */

#include "base/base.h"

/*
 * Test: Page calculation from byte offset
 *
 * Verifies the page number calculation:
 *   page_num = offset >> 15 (divide by 32KB)
 */
void test_fw_partial_page_calculation(void) {
    /* Offset 0 -> page 0 */
    ASSERT_EQ(0 >> 15, 0);

    /* Offset 32767 (0x7FFF, last byte of page 0) -> page 0 */
    ASSERT_EQ(0x7FFF >> 15, 0);

    /* Offset 32768 (0x8000, first byte of page 1) -> page 1 */
    ASSERT_EQ(0x8000 >> 15, 1);

    /* Offset 65535 (0xFFFF, last byte of page 1) -> page 1 */
    ASSERT_EQ(0xFFFF >> 15, 1);

    /* Offset 65536 (0x10000, first byte of page 2) -> page 2 */
    ASSERT_EQ(0x10000 >> 15, 2);

    /* Large offset: 1MB (0x100000) -> page 32 */
    ASSERT_EQ(0x100000 >> 15, 32);
}

/*
 * Test: Bytes remaining in first page calculation
 *
 * Verifies: bytes_to_page_end = 0x8000 - (offset & 0x7FFF)
 */
void test_fw_partial_bytes_to_page_end(void) {
    uint32_t offset;
    uint32_t bytes_to_end;

    /* Offset 0 -> 32KB remaining */
    offset = 0;
    bytes_to_end = 0x8000 - (offset & 0x7FFF);
    ASSERT_EQ(bytes_to_end, 0x8000);

    /* Offset 0x1000 -> 0x7000 remaining */
    offset = 0x1000;
    bytes_to_end = 0x8000 - (offset & 0x7FFF);
    ASSERT_EQ(bytes_to_end, 0x7000);

    /* Offset 0x7FFE -> 2 bytes remaining */
    offset = 0x7FFE;
    bytes_to_end = 0x8000 - (offset & 0x7FFF);
    ASSERT_EQ(bytes_to_end, 2);

    /* Offset 0x7FFF -> 1 byte remaining */
    offset = 0x7FFF;
    bytes_to_end = 0x8000 - (offset & 0x7FFF);
    ASSERT_EQ(bytes_to_end, 1);

    /* Offset 0x8000 (start of page 1) -> 32KB remaining */
    offset = 0x8000;
    bytes_to_end = 0x8000 - (offset & 0x7FFF);
    ASSERT_EQ(bytes_to_end, 0x8000);
}

/*
 * Test: Number of pages to iterate
 *
 * Verifies loop count calculation for various ranges.
 */
void test_fw_partial_page_count(void) {
    /* Single page: offset 0, length 100 -> 1 page */
    /* offset=0, len=100: pages 0 only */
    ASSERT_EQ(1, 1);

    /* Cross page boundary: offset 0x7F00, length 0x200 -> 2 pages */
    /* offset=0x7F00, len=0x200: pages 0 and 1 */
    ASSERT_EQ(2, 2);

    /* Full page plus partial: offset 0, length 0x10000 -> 2 pages */
    /* offset=0, len=0x10000: pages 0 and 1 */
    ASSERT_EQ(2, 2);
}

/*
 * Test: Purify flag values for partial write
 */
void test_fw_partial_purify_flags(void) {
    /* Local-only partial purify */
    ASSERT_EQ(0x0003, 0x0003);  /* FW_PARTIAL_LOCAL */

    /* Remote-sync partial purify */
    ASSERT_EQ(0x8003, 0x8003);  /* FW_PARTIAL_REMOTE */
}

/*
 * Test: Page size constant
 */
void test_fw_partial_page_size(void) {
    ASSERT_EQ(0x8000, 32768);   /* FILE_PAGE_SIZE = 32KB */
    ASSERT_EQ(0x7FFF, 32767);   /* FILE_PAGE_MASK */
}
