/*
 * dir/test/test_calc_entry_size.c - Unit tests for dir_$calc_entry_size
 *
 * Tests the directory entry size calculation function.
 * dir_$calc_entry_size is a pure function (aside from the global
 * DIR_$NAME_OFFSET_TABLE lookup), so we mock the table and test directly.
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

/*
 * Mock DIR_$NAME_OFFSET_TABLE (normally A5-relative global data on M68K).
 * Indexed by entry type & 7, gives the byte offset from entry start
 * to where the name begins (i.e., the fixed header size for that type).
 *
 * Representative values based on analysis of the directory code:
 *   type 0: 4  (B-tree interior ref)
 *   type 1: 4  (B-tree interior ref)
 *   type 2: 18 (file entry)
 *   type 3: 18 (hard link entry)
 *   type 4: 6  (soft link entry)
 *   types 5-7: 0 (unused)
 */
int16_t DIR_$NAME_OFFSET_TABLE[8] = {
    4, 4, 18, 18, 6, 0, 0, 0,
};

/* Prevent the real dir_internal.h from being pulled in;
 * we've already provided everything calc_entry_size.c needs. */
#define DIR_INTERNAL_H

/* Pull in the implementation directly */
#include "../calc_entry_size.c"

/* Test: type 2 (file) entry, name length 10 => 18 + 10 = 28, already aligned */
TEST(file_entry_aligned)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 2;   /* type 2 = file */
    entry[1] = 10;  /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(28, result);  /* 18 + 10 = 28, already 4-byte aligned */
}

/* Test: type 2 (file) entry, name length 7 => 18 + 7 = 25, rounds to 28 */
TEST(file_entry_unaligned)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 2;  /* type 2 = file */
    entry[1] = 7;  /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(28, result);  /* (25 + 3) & ~3 = 28 */
}

/* Test: type 3 (hard link) entry, name length 5 => 18 + 5 = 23, rounds to 24 */
TEST(hard_link_entry)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 3;  /* type 3 = hard link */
    entry[1] = 5;  /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(24, result);  /* (23 + 3) & ~3 = 24 */
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
    ASSERT_EQ(16, result);  /* (6 + 8 + 3) & ~3 = 16, no inline data added */
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
    ASSERT_EQ(36, result);  /* (6 + 8 + 20 + 3) & ~3 = 36 */
}

/* Test: type field masked correctly (upper bits in byte 0 ignored) */
TEST(type_mask)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 0xFA;  /* bits 0-2 = 2 (file), upper bits set */
    entry[1] = 6;     /* name length */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(24, result);  /* type=2: (18 + 6 + 3) & ~3 = 24 */
}

/* Test: name length 0 => just header, still aligned */
TEST(zero_name_length)
{
    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    entry[0] = 2;  /* type 2 = file */
    entry[1] = 0;  /* name length = 0 */

    uint16_t result = dir_$calc_entry_size(entry);
    ASSERT_EQ(20, result);  /* (18 + 0 + 3) & ~3 = 20 */
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
    ASSERT_EQ(12, result);  /* (6 + 5 + 0 + 3) & ~3 = 12 */
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
