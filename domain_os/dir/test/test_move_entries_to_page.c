/*
 * dir/test/test_move_entries_to_page.c - Unit tests for dir_$move_entries_to_page
 *
 * Tests the entry-moving function used during B-tree page splits.
 * We mock the dir_insert_ctx_t and set up fake pages with known entries,
 * then verify entries are correctly copied and source entries marked dead.
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

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("FAILED\n    Memory mismatch at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * Mock DIR_$NAME_OFFSET_TABLE
 *   type 0: 4  (B-tree interior ref)
 *   type 1: 4  (B-tree interior ref)
 *   type 2: 18 (file entry)
 *   type 3: 18 (hard link entry)
 *   type 4: 6  (soft link entry)
 */
int16_t DIR_$NAME_OFFSET_TABLE[8] = {
    4, 4, 18, 18, 6, 0, 0, 0,
};

/* Prevent real headers from being included */
#define DIR_INTERNAL_H

/* Minimal dir_insert_ctx_t - only fields used by move_entries_to_page */
typedef struct dir_insert_ctx {
    uint32_t    handle;
    void       *name;
    uint16_t    name_len;
    uint16_t    entry_type;
    uint32_t    extra_val;
    void       *uid;
    uint16_t    link_len;
    void       *link_data;
    int16_t     overflow_page;
    int16_t     max_depth;
    int16_t     path_page[9];
    int16_t     path_entry[9];
    uint32_t    dir_uid_high;
    uint32_t    dir_uid_low;
    int16_t     split_pages[16];
    int16_t     page_count;
    uint8_t    *page_data;
    uint8_t    *idx_base;
    uint8_t    *new_page;
    uint8_t    *inter_page;
    uint8_t    *temp_entry;
    int16_t     free_space;
    uint8_t     fim_data[16];
    uint8_t     remove_uid[8];
} dir_insert_ctx_t;

/* Include implementations directly */
#include "../calc_entry_size.c"
#include "../move_entries_to_page.c"

/*
 * Helper: Build a test page with file entries (type 2).
 *
 * Page layout:
 *   [0x00] flags byte
 *   [0x0E] end-of-index-table offset (int16_t)
 *   [0x10] start-of-free-space offset (int16_t)
 *   [0x12] index table starts here (each entry = int16_t offset to entry data)
 *   Entry data grows downward from offset 0x400
 *
 * Each type-2 file entry: 18-byte header + name_len, aligned to 4 bytes.
 * For name_len=6: 18 + 6 = 24 bytes (already 4-byte aligned).
 */
#define PAGE_SIZE 0x400

static void build_test_page(uint8_t *page, int num_entries, const char *names[],
                            uint8_t name_lens[])
{
    memset(page, 0, PAGE_SIZE);

    int16_t idx_end = 0x12 + num_entries * 2;  /* end of index table */
    int16_t free_off = PAGE_SIZE;              /* free space starts at end */

    for (int i = 0; i < num_entries; i++) {
        /* Compute aligned entry size for type 2 */
        int16_t entry_size = (18 + name_lens[i] + 3) & ~3;
        free_off -= entry_size;

        /* Write index entry (offset to entry data) */
        *(int16_t *)(page + 0x12 + i * 2) = free_off;

        /* Write entry data */
        uint8_t *entry = page + free_off;
        entry[0] = 2;              /* type 2 = file */
        entry[1] = name_lens[i];   /* name length */
        /* Bytes 2-17: header padding (zeroed) */
        /* Name starts at offset 18 */
        memcpy(entry + 18, names[i], name_lens[i]);
    }

    *(int16_t *)(page + 0x0E) = idx_end;
    *(int16_t *)(page + 0x10) = free_off;
}

static void build_empty_page(uint8_t *page)
{
    memset(page, 0, PAGE_SIZE);
    *(int16_t *)(page + 0x0E) = 0x12;       /* index table starts at 0x12 */
    *(int16_t *)(page + 0x10) = PAGE_SIZE;   /* all space is free */
}

/* Test: from_idx > to_idx does nothing */
TEST(noop_when_from_gt_to)
{
    uint8_t src_page[PAGE_SIZE];
    uint8_t dst_page[PAGE_SIZE];
    const char *names[] = { "alpha" };
    uint8_t lens[] = { 5 };

    build_test_page(src_page, 1, names, lens);
    build_empty_page(dst_page);

    uint8_t src_copy[PAGE_SIZE];
    uint8_t dst_copy[PAGE_SIZE];
    memcpy(src_copy, src_page, PAGE_SIZE);
    memcpy(dst_copy, dst_page, PAGE_SIZE);

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = src_page;
    ctx.idx_base = src_page + 0x12;
    ctx.new_page = dst_page;

    dir_$move_entries_to_page(&ctx, 3, 1);

    /* Pages should be unchanged */
    ASSERT_MEM_EQ(src_copy, src_page, PAGE_SIZE);
    ASSERT_MEM_EQ(dst_copy, dst_page, PAGE_SIZE);
}

/* Test: Move a single entry (from_idx == to_idx) */
TEST(move_single_entry)
{
    uint8_t src_page[PAGE_SIZE];
    uint8_t dst_page[PAGE_SIZE];
    const char *names[] = { "hello!", "worldx" };
    uint8_t lens[] = { 6, 6 };

    build_test_page(src_page, 2, names, lens);
    build_empty_page(dst_page);

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = src_page;
    ctx.idx_base = src_page + 0x12;
    ctx.new_page = dst_page;

    /* Move entry 1 only (1-based) */
    dir_$move_entries_to_page(&ctx, 1, 1);

    /* Source page should have reclaimable bit set */
    ASSERT_EQ(0x20, src_page[0] & 0x20);

    /* Source entry 1 should be marked dead (bit 7) */
    int16_t src_entry_off = *(int16_t *)(src_page + 0x12);
    ASSERT_EQ(0x80, src_page[src_entry_off] & 0x80);

    /* Source entry 2 should NOT be marked dead */
    int16_t src_entry2_off = *(int16_t *)(src_page + 0x14);
    ASSERT_EQ(0, src_page[src_entry2_off] & 0x80);

    /* Destination page should have 1 entry in index table */
    ASSERT_EQ(0x14, *(int16_t *)(dst_page + 0x0E));  /* 0x12 + 2 */

    /* Destination entry should match source entry data (minus the dead flag) */
    int16_t dst_entry_off = *(int16_t *)(dst_page + 0x12);
    /* Type and name length */
    ASSERT_EQ(2, dst_page[dst_entry_off] & 7);        /* type 2 */
    ASSERT_EQ(6, dst_page[dst_entry_off + 1]);          /* name len */
    /* Name content */
    ASSERT_MEM_EQ("hello!", dst_page + dst_entry_off + 18, 6);
}

/* Test: Move multiple consecutive entries */
TEST(move_multiple_entries)
{
    uint8_t src_page[PAGE_SIZE];
    uint8_t dst_page[PAGE_SIZE];
    const char *names[] = { "aaa", "bbb", "ccc", "ddd" };
    uint8_t lens[] = { 3, 3, 3, 3 };
    /* type 2, name_len 3: 18 + 3 = 21, aligned = 24 bytes each */

    build_test_page(src_page, 4, names, lens);
    build_empty_page(dst_page);

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = src_page;
    ctx.idx_base = src_page + 0x12;
    ctx.new_page = dst_page;

    /* Move entries 2 through 3 (1-based) */
    dir_$move_entries_to_page(&ctx, 2, 3);

    /* Source entries 2 and 3 should be dead */
    int16_t off2 = *(int16_t *)(src_page + 0x12 + 1*2);
    int16_t off3 = *(int16_t *)(src_page + 0x12 + 2*2);
    ASSERT_EQ(0x80, src_page[off2] & 0x80);
    ASSERT_EQ(0x80, src_page[off3] & 0x80);

    /* Source entries 1 and 4 should NOT be dead */
    int16_t off1 = *(int16_t *)(src_page + 0x12 + 0*2);
    int16_t off4 = *(int16_t *)(src_page + 0x12 + 3*2);
    ASSERT_EQ(0, src_page[off1] & 0x80);
    ASSERT_EQ(0, src_page[off4] & 0x80);

    /* Destination should have 2 entries */
    ASSERT_EQ(0x16, *(int16_t *)(dst_page + 0x0E));  /* 0x12 + 4 */

    /* Free space should have decreased by 2 * 24 = 48 bytes */
    ASSERT_EQ(PAGE_SIZE - 48, *(int16_t *)(dst_page + 0x10));

    /* Verify destination entry names */
    int16_t dst_off1 = *(int16_t *)(dst_page + 0x12);
    int16_t dst_off2 = *(int16_t *)(dst_page + 0x14);
    ASSERT_MEM_EQ("bbb", dst_page + dst_off1 + 18, 3);
    ASSERT_MEM_EQ("ccc", dst_page + dst_off2 + 18, 3);
}

/* Test: Reclaimable bit set on source page */
TEST(reclaimable_bit_set)
{
    uint8_t src_page[PAGE_SIZE];
    uint8_t dst_page[PAGE_SIZE];
    const char *names[] = { "test" };
    uint8_t lens[] = { 4 };

    build_test_page(src_page, 1, names, lens);
    build_empty_page(dst_page);

    /* Ensure bit 5 is initially clear */
    src_page[0] = 0x00;

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = src_page;
    ctx.idx_base = src_page + 0x12;
    ctx.new_page = dst_page;

    dir_$move_entries_to_page(&ctx, 1, 1);

    ASSERT_EQ(0x20, src_page[0] & 0x20);
}

/* Test: Source page flags byte preserves other bits when setting reclaimable */
TEST(preserves_other_flags)
{
    uint8_t src_page[PAGE_SIZE];
    uint8_t dst_page[PAGE_SIZE];
    const char *names[] = { "test" };
    uint8_t lens[] = { 4 };

    build_test_page(src_page, 1, names, lens);
    build_empty_page(dst_page);

    /* Set some other bits */
    src_page[0] = 0xC2;  /* bits 7, 6, 1 set */

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = src_page;
    ctx.idx_base = src_page + 0x12;
    ctx.new_page = dst_page;

    dir_$move_entries_to_page(&ctx, 1, 1);

    /* 0xC2 | 0x20 = 0xE2 */
    ASSERT_EQ(0xE2, src_page[0]);
}

/* Test: Move all entries from page */
TEST(move_all_entries)
{
    uint8_t src_page[PAGE_SIZE];
    uint8_t dst_page[PAGE_SIZE];
    const char *names[] = { "aa", "bb", "cc" };
    uint8_t lens[] = { 2, 2, 2 };
    /* type 2, name_len 2: 18 + 2 = 20, already 4-byte aligned */

    build_test_page(src_page, 3, names, lens);
    build_empty_page(dst_page);

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = src_page;
    ctx.idx_base = src_page + 0x12;
    ctx.new_page = dst_page;

    dir_$move_entries_to_page(&ctx, 1, 3);

    /* All source entries should be dead */
    for (int i = 0; i < 3; i++) {
        int16_t off = *(int16_t *)(src_page + 0x12 + i * 2);
        ASSERT_EQ(0x80, src_page[off] & 0x80);
    }

    /* Destination should have 3 entries */
    ASSERT_EQ(0x18, *(int16_t *)(dst_page + 0x0E));  /* 0x12 + 6 */

    /* Free space decreased by 3 * 20 = 60 */
    ASSERT_EQ(PAGE_SIZE - 60, *(int16_t *)(dst_page + 0x10));

    /* Verify all names are present */
    int16_t d1 = *(int16_t *)(dst_page + 0x12);
    int16_t d2 = *(int16_t *)(dst_page + 0x14);
    int16_t d3 = *(int16_t *)(dst_page + 0x16);
    ASSERT_MEM_EQ("aa", dst_page + d1 + 18, 2);
    ASSERT_MEM_EQ("bb", dst_page + d2 + 18, 2);
    ASSERT_MEM_EQ("cc", dst_page + d3 + 18, 2);
}

/* Test: Destination page free space tracking is correct */
TEST(dst_free_space_tracking)
{
    uint8_t src_page[PAGE_SIZE];
    uint8_t dst_page[PAGE_SIZE];
    /* Use different name lengths to get different entry sizes */
    const char *names[] = { "short", "longernam" };
    uint8_t lens[] = { 5, 9 };
    /* type 2, name 5: 18+5=23, aligned=24 */
    /* type 2, name 9: 18+9=27, aligned=28 */

    build_test_page(src_page, 2, names, lens);
    build_empty_page(dst_page);

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = src_page;
    ctx.idx_base = src_page + 0x12;
    ctx.new_page = dst_page;

    dir_$move_entries_to_page(&ctx, 1, 2);

    /* Free space should be PAGE_SIZE - 24 - 28 = PAGE_SIZE - 52 */
    ASSERT_EQ(PAGE_SIZE - 52, *(int16_t *)(dst_page + 0x10));

    /* Index table should have grown by 4 bytes (2 entries) */
    ASSERT_EQ(0x16, *(int16_t *)(dst_page + 0x0E));
}

int main(void)
{
    printf("dir_$move_entries_to_page tests:\n");

    RUN_TEST(noop_when_from_gt_to);
    RUN_TEST(move_single_entry);
    RUN_TEST(move_multiple_entries);
    RUN_TEST(reclaimable_bit_set);
    RUN_TEST(preserves_other_flags);
    RUN_TEST(move_all_entries);
    RUN_TEST(dst_free_space_tracking);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
