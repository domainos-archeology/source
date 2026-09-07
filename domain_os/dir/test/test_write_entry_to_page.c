/*
 * dir/test/test_write_entry_to_page.c - Unit tests for dir_$write_entry_to_page
 *
 * Tests the directory entry write function. We mock the globals and stubs
 * needed, then verify that entries are correctly written to page buffers.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

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
 *
 * Actual binary values from A5(0xE7DC00)+0x2000 = 0xE7FC00:
 *   type 0: 0   (unused)
 *   type 1: 4   (internal B-tree pointer: 2 header + 2 child page)
 *   type 2: 16  (file entry: 2 header + 2 reserved + 8 UID + 4 reserved)
 *   type 3: 20  (hard link: 2 header + 2 reserved + 8 UID + 4 extra + 4 reserved)
 *   type 4: 12  (soft link: 2 header + 2 link_len + 2 overflow + 6 reserved)
 *   types 5-7: 0 (unused)
 */
int16_t DIR_$NAME_OFFSET_TABLE[8] = {
    0, 4, 16, 20, 12, 0, 0, 0,
};

/* CRASH_SYSTEM stub - records that it was called, then longjmps out.
 * In the real system CRASH_SYSTEM never returns; we simulate that with longjmp. */
static int crash_called = 0;
static jmp_buf crash_jmpbuf;
typedef uint32_t status_$t;

/* Mock error status cell (declared status_$t in dir/dir_internal.h) */
status_$t Naming_bad_request_header_ver_err = 0;

void CRASH_SYSTEM(const status_$t *msg) {
    (void)msg;
    crash_called = 1;
    longjmp(crash_jmpbuf, 1);
}

/* dir_$copy_name_cross_page stub - records call params */
static int cross_page_copy_called = 0;
static uint32_t cross_page_handle;
static int16_t cross_page_src_page;
static int16_t cross_page_src_offset;
static int16_t cross_page_dest_page;
static int16_t cross_page_dest_offset;
static int16_t cross_page_byte_count;

void dir_$copy_name_cross_page(uint32_t handle, int16_t src_page,
                               int16_t src_offset, int16_t dest_page,
                               int16_t dest_offset, int16_t byte_count) {
    cross_page_copy_called = 1;
    cross_page_handle = handle;
    cross_page_src_page = src_page;
    cross_page_src_offset = src_offset;
    cross_page_dest_page = dest_page;
    cross_page_dest_offset = dest_offset;
    cross_page_byte_count = byte_count;
}

/* Minimal uid_t - avoid conflict with system uid_t */
#define uid_t dir_uid_t
typedef struct { uint32_t high; uint32_t low; } dir_uid_t;

/* Minimal dir_insert_ctx_t - enough fields for testing */
typedef struct {
    uint32_t    handle;
    void       *name;
    uint16_t    name_len;
    uint16_t    entry_type;
    uint32_t    extra_val;
    uid_t      *uid;
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

/* Prevent real headers from being included */
#define DIR_INTERNAL_H

/* Pull in the implementation */
#include "../write_entry_to_page.c"

/* Helper: initialize a page buffer for testing */
static void init_page(uint8_t *page, int16_t page_link, int16_t root_extra) {
    memset(page, 0, 1024);
    /* page[0]: flags/type (0 = leaf, 0x40 = internal) */
    *(int16_t *)(page + 0x0A) = page_link;        /* page link (0 = root) */
    *(int16_t *)(page + 0x0E) = 0x12;             /* index table start */
    *(int16_t *)(page + 0x10) = 0x400;             /* free space at end (1024) */
    if (page_link == 0) {
        *(int16_t *)(page + 0x14) = root_extra;    /* root header extra */
    }
}

/* Helper: reset stubs */
static void reset_stubs(void) {
    crash_called = 0;
    cross_page_copy_called = 0;
}

/*
 * Test: Write a type 2 (file) entry to a leaf page
 */
TEST(leaf_type2_file_entry)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);  /* non-root leaf page */

    uint8_t *page_ptr = page;
    uid_t test_uid = { 0xDEADBEEF, 0xCAFEBABE };
    char name[] = "testfile";
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 2;
    ctx.uid = &test_uid;
    ctx.name = name;
    ctx.name_len = 8;

    /* aligned_size = (TABLE[2] + 8 + 3) & ~3 = (16 + 8 + 3) & ~3 = 24 */
    uint16_t aligned_size = 24;

    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 8, aligned_size, 0);

    /* Check page header updates */
    ASSERT_EQ(0x400 - 24, *(int16_t *)(page + 0x10));  /* free space moved down */
    ASSERT_EQ(0x14, *(int16_t *)(page + 0x0E));         /* index table grew by 2 */

    /* Check index entry points to the new entry */
    int16_t entry_off = *(int16_t *)(page + 0x12);
    ASSERT_EQ(0x400 - 24, entry_off);

    /* Check entry contents */
    uint8_t *entry = page + entry_off;
    ASSERT_EQ(2, entry[0] & 7);     /* type 2 */
    ASSERT_EQ(8, entry[1]);          /* name_len */
    ASSERT_EQ(0, entry[2]);          /* reserved */
    ASSERT_EQ(0, entry[3]);          /* reserved */

    /* Check UID */
    ASSERT_EQ(0xDEADBEEF, *(uint32_t *)(entry + 4));
    ASSERT_EQ(0xCAFEBABE, *(uint32_t *)(entry + 8));

    /* Check cleared bytes */
    ASSERT_EQ(0, *(uint32_t *)(entry + 0x0C));

    /* Check name at offset 16 (TABLE[2]) */
    ASSERT_MEM_EQ("testfile", entry + 16, 8);
}

/*
 * Test: Write a type 3 (hard link) entry to a leaf page
 */
TEST(leaf_type3_hard_link)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);

    uint8_t *page_ptr = page;
    uid_t test_uid = { 0x11223344, 0x55667788 };
    char name[] = "link1";
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 3;
    ctx.uid = &test_uid;
    ctx.extra_val = 0xAABBCCDD;
    ctx.name = name;
    ctx.name_len = 5;

    /* aligned_size = (TABLE[3] + 5 + 3) & ~3 = (20 + 5 + 3) & ~3 = 28 */
    uint16_t aligned_size = 28;

    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 5, aligned_size, 0);

    int16_t entry_off = *(int16_t *)(page + 0x12);
    uint8_t *entry = page + entry_off;

    ASSERT_EQ(3, entry[0] & 7);
    ASSERT_EQ(5, entry[1]);
    ASSERT_EQ(0, *(uint16_t *)(entry + 2));    /* reserved cleared */
    ASSERT_EQ(0x11223344, *(uint32_t *)(entry + 4));
    ASSERT_EQ(0x55667788, *(uint32_t *)(entry + 8));
    ASSERT_EQ(0xAABBCCDD, *(uint32_t *)(entry + 0x0C));
    ASSERT_EQ(0, *(uint32_t *)(entry + 0x10));  /* cleared */

    /* Name at offset 20 (TABLE[3]) */
    ASSERT_MEM_EQ("link1", entry + 20, 5);
}

/*
 * Test: Write a type 4 (soft link) entry with overflow page (not inline)
 */
TEST(leaf_type4_overflow)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);

    uint8_t *page_ptr = page;
    char name[] = "symlink";
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 4;
    ctx.link_len = 100;
    ctx.overflow_page = 5;
    ctx.name = name;
    ctx.name_len = 7;

    /* aligned_size = (TABLE[4] + 7 + 3) & ~3 = (12 + 7 + 3) & ~3 = 20 */
    uint16_t aligned_size = 20;

    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 7, aligned_size, 0);

    int16_t entry_off = *(int16_t *)(page + 0x12);
    uint8_t *entry = page + entry_off;

    ASSERT_EQ(4, entry[0] & 7);
    ASSERT_EQ(7, entry[1]);
    ASSERT_EQ(100, *(uint16_t *)(entry + 2));    /* link_len */
    ASSERT_EQ(5, *(int16_t *)(entry + 4));       /* overflow_page */
    ASSERT_EQ(0, *(uint32_t *)(entry + 6));      /* cleared */
    ASSERT_EQ(0, *(uint16_t *)(entry + 0x0A));   /* cleared */

    /* Name at offset 12 (TABLE[4]) */
    ASSERT_MEM_EQ("symlink", entry + 12, 7);
}

/*
 * Test: Write a type 4 entry with inline link data (overflow_page == -1)
 */
TEST(leaf_type4_inline_link)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);

    uint8_t *page_ptr = page;
    char name[] = "lnk";
    char link_target[] = "/usr/local";
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 4;
    ctx.link_len = 10;
    ctx.overflow_page = -1;
    ctx.link_data = link_target;
    ctx.name = name;
    ctx.name_len = 3;

    /* aligned_size = (TABLE[4] + 3 + 10 + 3) & ~3 = (12 + 3 + 10 + 3) & ~3 = 28 */
    uint16_t aligned_size = 28;

    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 3, aligned_size, 0);

    int16_t entry_off = *(int16_t *)(page + 0x12);
    uint8_t *entry = page + entry_off;

    ASSERT_EQ(4, entry[0] & 7);
    ASSERT_EQ(3, entry[1]);
    ASSERT_EQ(10, *(uint16_t *)(entry + 2));     /* link_len */
    ASSERT_EQ(-1, *(int16_t *)(entry + 4));      /* overflow_page = -1 */

    /* Name at offset 12 (TABLE[4]) */
    ASSERT_MEM_EQ("lnk", entry + 12, 3);

    /* Link data at entry + 0x0C + name_len = entry + 12 + 3 = entry + 15 */
    ASSERT_MEM_EQ("/usr/local", entry + 12 + 3, 10);
}

/*
 * Test: Write an internal page entry (type 1 with child pointer)
 */
TEST(internal_page_entry_flag_positive)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);
    page[0] = 0x40;  /* Set page type to internal (bits 6-7 = 01) */

    uint8_t *page_ptr = page;
    char name[] = "abc";
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = name;
    ctx.name_len = 3;
    ctx.page_count = 2;
    /* split_pages[page_count + 1] = split_pages[3] */
    ctx.split_pages[3] = 42;
    ctx.split_pages[4] = 99;

    /* aligned_size = (TABLE[1] + 3 + 3) & ~3 = (4 + 3 + 3) & ~3 = 8 */
    uint16_t aligned_size = 8;

    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 3, aligned_size, 0);

    int16_t entry_off = *(int16_t *)(page + 0x12);
    uint8_t *entry = page + entry_off;

    ASSERT_EQ(1, entry[0] & 7);       /* type 1 */
    ASSERT_EQ(3, entry[1]);            /* name_len */
    ASSERT_EQ(42, *(uint16_t *)(entry + 2));  /* child page = split_pages[3] */

    /* Name at offset 4 (TABLE[1]) */
    ASSERT_MEM_EQ("abc", entry + 4, 3);
}

/*
 * Test: Internal page entry with negative flag uses split_pages[page_count+2]
 */
TEST(internal_page_entry_flag_negative)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);
    page[0] = 0x40;  /* internal page */

    uint8_t *page_ptr = page;
    char name[] = "xyz";
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = name;
    ctx.name_len = 3;
    ctx.page_count = 1;
    ctx.split_pages[2] = 55;   /* page_count + 1 */
    ctx.split_pages[3] = 77;   /* page_count + 2 */

    uint16_t aligned_size = 8;

    dir_$write_entry_to_page(&ctx, 0xFF, &page_ptr, 1, 3, aligned_size, 0);

    int16_t entry_off = *(int16_t *)(page + 0x12);
    uint8_t *entry = page + entry_off;

    ASSERT_EQ(1, entry[0] & 7);
    ASSERT_EQ(77, *(uint16_t *)(entry + 2));  /* child page = split_pages[3] */
}

/*
 * Test: Root page adjusts index entry by root header extra offset
 */
TEST(root_page_offset_adjustment)
{
    reset_stubs();
    uint8_t page[1024];
    /* Root page: page_link=0, root_extra=0x20 (32 bytes of root header) */
    init_page(page, 0, 0x20);

    uint8_t *page_ptr = page;
    uid_t test_uid = { 0x12345678, 0x9ABCDEF0 };
    char name[] = "root";
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 2;
    ctx.uid = &test_uid;
    ctx.name = name;
    ctx.name_len = 4;

    /* aligned_size = (16 + 4 + 3) & ~3 = 20 */
    uint16_t aligned_size = 20;

    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 4, aligned_size, 0);

    /* Index entry should be at 0x12 + root_extra(0x20) = 0x32 (for entry 1, offset 0) */
    int16_t idx_val = *(int16_t *)(page + 0x12 + 0x20);
    ASSERT_EQ(0x400 - 20, idx_val);

    /* Verify the entry data */
    uint8_t *entry = page + idx_val;
    ASSERT_EQ(2, entry[0] & 7);
    ASSERT_EQ(4, entry[1]);
    ASSERT_EQ(0x12345678, *(uint32_t *)(entry + 4));
    ASSERT_EQ(0x9ABCDEF0, *(uint32_t *)(entry + 8));
    ASSERT_MEM_EQ("root", entry + 16, 4);
}

/*
 * Test: Multiple entries update page headers correctly
 */
TEST(multiple_entries)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);

    uint8_t *page_ptr = page;
    uid_t uid1 = { 0x11111111, 0x22222222 };
    uid_t uid2 = { 0x33333333, 0x44444444 };
    char name1[] = "aaa";
    char name2[] = "bbb";
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 2;

    /* First entry */
    ctx.uid = &uid1;
    ctx.name = name1;
    ctx.name_len = 3;
    uint16_t aligned1 = (16 + 3 + 3) & ~3;  /* 20 */
    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 3, aligned1, 0);

    /* Check after first entry */
    ASSERT_EQ(0x400 - 20, *(int16_t *)(page + 0x10));
    ASSERT_EQ(0x14, *(int16_t *)(page + 0x0E));

    /* Second entry */
    ctx.uid = &uid2;
    ctx.name = name2;
    ctx.name_len = 3;
    uint16_t aligned2 = 20;
    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 2, 3, aligned2, 0);

    /* Check after second entry */
    ASSERT_EQ(0x400 - 40, *(int16_t *)(page + 0x10));  /* two entries */
    ASSERT_EQ(0x16, *(int16_t *)(page + 0x0E));          /* index grew by 4 */

    /* Verify both entries */
    uint8_t *e1 = page + *(int16_t *)(page + 0x12);
    uint8_t *e2 = page + *(int16_t *)(page + 0x14);

    ASSERT_EQ(0x11111111, *(uint32_t *)(e1 + 4));
    ASSERT_EQ(0x33333333, *(uint32_t *)(e2 + 4));
    ASSERT_MEM_EQ("aaa", e1 + 16, 3);
    ASSERT_MEM_EQ("bbb", e2 + 16, 3);
}

/*
 * Test: Cross-page name copy (src_name_loc != 0)
 */
TEST(cross_page_name_copy)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 5, 0);  /* page_link = 5 */

    uint8_t *page_ptr = page;
    uid_t test_uid = { 0, 0 };
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 2;
    ctx.uid = &test_uid;
    ctx.handle = 0x12340000;

    uint16_t aligned_size = 20;
    /* src_name_loc = (page 3 << 10) | offset 0x100 = 0xC00 | 0x100 = 0xD00 */
    uint32_t src_name_loc = (3 << 10) | 0x100;

    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 4, aligned_size, src_name_loc);

    /* Verify cross-page copy was called with correct params */
    ASSERT_EQ(1, cross_page_copy_called);
    ASSERT_EQ(0x12340000, cross_page_handle);
    ASSERT_EQ(3, cross_page_src_page);
    ASSERT_EQ(0x100, cross_page_src_offset);
    ASSERT_EQ(5, cross_page_dest_page);         /* page_link */
    ASSERT_EQ(4, cross_page_byte_count);         /* name_len */
}

/*
 * Test: Type 1 on leaf page causes crash
 */
TEST(leaf_type1_crashes)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);

    uint8_t *page_ptr = page;
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 1;

    uint16_t aligned_size = 8;

    /* CRASH_SYSTEM never returns in the real system; our stub uses longjmp
     * to simulate that, preventing the function from continuing into the
     * name_copy path with null pointers. */
    if (setjmp(crash_jmpbuf) == 0) {
        dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 1, aligned_size, 0);
        /* If we get here, CRASH_SYSTEM wasn't called */
        ASSERT_EQ(1, crash_called);  /* will fail */
    }

    ASSERT_EQ(1, crash_called);
}

/*
 * Test: Zero name length (no name bytes copied)
 */
TEST(zero_name_length)
{
    reset_stubs();
    uint8_t page[1024];
    init_page(page, 1, 0);

    uint8_t *page_ptr = page;
    uid_t test_uid = { 0xAAAAAAAA, 0xBBBBBBBB };
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_type = 2;
    ctx.uid = &test_uid;
    ctx.name = "x";  /* won't be read since name_len=0 */

    uint16_t aligned_size = 16;  /* (16 + 0 + 3) & ~3 = 16 */

    dir_$write_entry_to_page(&ctx, 0, &page_ptr, 1, 0, aligned_size, 0);

    int16_t entry_off = *(int16_t *)(page + 0x12);
    uint8_t *entry = page + entry_off;

    ASSERT_EQ(2, entry[0] & 7);
    ASSERT_EQ(0, entry[1]);          /* name_len = 0 */
    ASSERT_EQ(0xAAAAAAAA, *(uint32_t *)(entry + 4));
}

int main(void)
{
    printf("dir_$write_entry_to_page tests:\n");

    RUN_TEST(leaf_type2_file_entry);
    RUN_TEST(leaf_type3_hard_link);
    RUN_TEST(leaf_type4_overflow);
    RUN_TEST(leaf_type4_inline_link);
    RUN_TEST(internal_page_entry_flag_positive);
    RUN_TEST(internal_page_entry_flag_negative);
    RUN_TEST(root_page_offset_adjustment);
    RUN_TEST(multiple_entries);
    RUN_TEST(cross_page_name_copy);
    RUN_TEST(leaf_type1_crashes);
    RUN_TEST(zero_name_length);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
