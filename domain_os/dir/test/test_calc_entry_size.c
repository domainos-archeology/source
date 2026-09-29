/*
 * dir/test/test_calc_entry_size.c - Unit tests for dir_$calc_entry_size
 *
 * Tests the directory entry size calculation function.
 * dir_$calc_entry_size is a pure function (aside from the global
 * DIR_$NAME_OFFSET_TABLE lookup, a DIR_$DATA field), so the test defines the
 * block with the image's table and tests directly.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#include "dir/dir_internal.h"

/*
 * DIR_$NAME_OFFSET_TABLE is the DIR block's field at A5+0x2000
 * (0xE7FC00), DIR_$DATA.name_offset_table; the block carries the image's
 * words.  Indexed by entry type & 7, it gives the byte offset from entry
 * start to where the name begins (the fixed header size for that type):
 *   type 0: 0   (unused)
 *   type 1: 4   (internal B-tree pointer: 2 header + 2 child page)
 *   type 2: 16  (file entry: 2 header + 2 reserved + 8 UID + 4 reserved)
 *   type 3: 20  (hard link: 2 header + 2 reserved + 8 UID + 4 extra + 4 reserved)
 *   type 4: 12  (soft link: 2 header + 2 link_len + 2 overflow + 6 reserved)
 *   types 5-7: 0 (unused)
 */
MODULE_DATA_DEFINE_INIT(dir_$data_t, DIR_$DATA, 0x00E7DBF8, {
    .name_offset_table = { 0, 4, 16, 20, 12, 0, 0, 0 },
});

/* Pull in the implementation directly */
#include "../calc_entry_size.c"

/* Test: type 2 (file) entry, name length 10 => 16 + 10 = 26, rounds to 28 */
TEST(file_entry_aligned)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 2;   /* type 2 = file */
    entry[1] = 10;  /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(28, result);  /* (26 + 3) & ~3 = 28 */
}

/* Test: type 2 (file) entry, name length 7 => 16 + 7 = 23, rounds to 24 */
TEST(file_entry_unaligned)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 2;  /* type 2 = file */
    entry[1] = 7;  /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(24, result);  /* (23 + 3) & ~3 = 24 */
}

/* Test: type 3 (hard link) entry, name length 5 => 20 + 5 = 25, rounds to 28 */
TEST(hard_link_entry)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 3;  /* type 3 = hard link */
    entry[1] = 5;  /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(28, result);  /* (25 + 3) & ~3 = 28 */
}

/* Test: type 4 (soft link) with overflow page (not -1), no inline data */
TEST(soft_link_no_inline)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 4;   /* type 4 = soft link */
    entry[1] = 8;   /* name length */
    /* bytes 2-3: link data length (irrelevant when not inline) */
    entry[2] = 0; entry[3] = 20;
    /* bytes 4-5: overflow page = 5 (not -1, so NOT inline) */
    entry[4] = 0; entry[5] = 5;

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(20, result);  /* (12 + 8 + 3) & ~3 = 20, no inline data added */
}

/* Test: type 4 (soft link) with inline data (overflow_page == -1) */
TEST(soft_link_inline)
{
    uint8_t entry[64];
    memset(entry, 0, sizeof(entry));
    entry[0] = 4;   /* type 4 = soft link */
    entry[1] = 8;   /* name length */
    /* bytes 2-3: link data length = 20 (native int16_t) */
    *(int16_t *)(entry + 2) = 20;
    /* bytes 4-5: overflow page = -1 (native int16_t) */
    *(int16_t *)(entry + 4) = -1;

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(40, result);  /* (12 + 8 + 20 + 3) & ~3 = 40 */
}

/* Test: type field masked correctly (upper bits in byte 0 ignored) */
TEST(type_mask)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 0xFA;  /* bits 0-2 = 2 (file), upper bits set */
    entry[1] = 6;     /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(24, result);  /* type=2: (16 + 6 + 3) & ~3 = 24 */
}

/* Test: name length 0 => just header, still aligned */
TEST(zero_name_length)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 2;  /* type 2 = file */
    entry[1] = 0;  /* name length = 0 */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(16, result);  /* (16 + 0 + 3) & ~3 = 16 */
}

/* Test: type 4 inline with zero-length link data */
TEST(soft_link_inline_zero_link)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 4;   /* type 4 = soft link */
    entry[1] = 5;   /* name length */
    /* bytes 2-3: link data length = 0 */
    *(int16_t *)(entry + 2) = 0;
    /* bytes 4-5: overflow page = -1 */
    *(int16_t *)(entry + 4) = -1;

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(20, result);  /* (12 + 5 + 0 + 3) & ~3 = 20 */
}

/* Test: type 1 (B-tree interior), name length 12 => 4 + 12 = 16, aligned */
TEST(btree_entry)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 1;   /* type 1 = B-tree interior */
    entry[1] = 12;  /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(16, result);  /* (4 + 12 + 3) & ~3 = 16 */
}

int main(void)
{
    printf("dir_$calc_entry_size tests:\n");

    RUN_TEST(file_entry_aligned);
    RUN_TEST(file_entry_unaligned);
    RUN_TEST(hard_link_entry);
    RUN_TEST(soft_link_no_inline);
    RUN_TEST(soft_link_inline);
    RUN_TEST(type_mask);
    RUN_TEST(zero_name_length);
    RUN_TEST(soft_link_inline_zero_link);
    RUN_TEST(btree_entry);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
