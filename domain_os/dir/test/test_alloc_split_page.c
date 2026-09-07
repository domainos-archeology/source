/*
 * dir/test/test_alloc_split_page.c - Unit tests for dir_$alloc_split_page
 *
 * Tests the page allocation for B-tree splitting. We mock dir_$map_page,
 * AST_$GET_SEG_MAP, UID_$GEN, dir_$purify_split_pages, and CRASH_SYSTEM,
 * then verify that split_pages[] is filled correctly, pages are copied,
 * and the directory UID and handle flags are updated.
 *
 * source-14k1: this file used to keep private copies of dir_insert_ctx_t,
 * uid_t and status_$t and to #define DIR_INTERNAL_H so the real header stayed
 * out.  The private context typed the directory handle as uintptr_t, so it
 * silently disagreed with the real one.  It now includes dir/dir_internal.h
 * and mocks only the callees, like dir/test/test_insert_entry.c does.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
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

#include "dir/dir_internal.h"

/* ================================================================
 * Mock globals and stubs
 * ================================================================ */

/* Mock error string */
char Naming_bad_request_header_ver_err = 0;

/* Mock DIR_$NAME_OFFSET_TABLE (not directly used by alloc_split_page,
 * but may be referenced via headers) */
int16_t DIR_$NAME_OFFSET_TABLE[8] = { 0, 4, 16, 20, 12, 0, 0, 0 };

/* CRASH_SYSTEM stub */
static int crash_called = 0;
static jmp_buf crash_jmpbuf;

void CRASH_SYSTEM(const status_$t *msg) {
    (void)msg;
    crash_called = 1;
    longjmp(crash_jmpbuf, 1);
}

/* ================================================================
 * Mock page pool - simulates directory pages
 * ================================================================ */
#define MAX_PAGES 32
#define PAGE_SIZE 1024
static uint8_t page_pool[MAX_PAGES][PAGE_SIZE];

/* Mock handle structure */
static uint8_t mock_handle[32];

static void init_handle(uint16_t total_pages) {
    memset(mock_handle, 0, sizeof(mock_handle));
    /* UID at offset 0: set to known values */
    *(uint32_t *)(mock_handle + 0) = 0xAABBCCDD;
    *(uint32_t *)(mock_handle + 4) = 0x11223344;
    /* Directory size at offset 0x10: total_pages * 1024 */
    *(uint32_t *)(mock_handle + 0x10) = (uint32_t)total_pages * PAGE_SIZE;
}

static void init_page_pool(void) {
    memset(page_pool, 0, sizeof(page_pool));
}

/* Mark a page as "in use" by setting a non-zero first word */
static void mark_page_used(int page_idx) {
    *(uint16_t *)page_pool[page_idx] = 0x0001;
}

/* dir_$map_page mock */
void *dir_$map_page(void *handle, int16_t page_idx) {
    (void)handle;
    if (page_idx < 0 || page_idx >= MAX_PAGES) {
        /* Return a zeroed page for out-of-range indices */
        static uint8_t zero_page[PAGE_SIZE];
        memset(zero_page, 0, PAGE_SIZE);
        return zero_page;
    }
    return page_pool[page_idx];
}

/* UID_$GEN mock - sets a known UID value */
static int uid_gen_called = 0;
void UID_$GEN(uid_t *uid_ret) {
    uid_gen_called = 1;
    uid_ret->high = 0xDEADBEEF;
    uid_ret->low = 0xCAFEBABE;
}

/* AST_$GET_SEG_MAP mock */
static uint32_t mock_seg_bitmap = 0xFFFFFFFF;  /* All pages in use by default */
static int seg_map_called = 0;
static status_$t seg_map_status = 0;

void AST_$GET_SEG_MAP(uint32_t *uid_info, uint32_t start_offset,
                      uint32_t unused, uid_t *vol_uid, uint32_t count,
                      uint16_t flags, uint32_t *output, status_$t *status) {
    (void)uid_info; (void)start_offset; (void)unused;
    (void)vol_uid; (void)count; (void)flags;
    seg_map_called = 1;
    output[0] = mock_seg_bitmap;
    memset(output + 1, 0xFF, 28);  /* Fill rest of buffer */
    *status = seg_map_status;
}

/* dir_$purify_split_pages mock */
static int purify_called = 0;
static status_$t purify_status = 0;

void dir_$purify_split_pages(dir_insert_ctx_t *ctx, status_$t *status_ret) {
    (void)ctx;
    purify_called = 1;
    *status_ret = purify_status;
}

/* AST_$PURIFY mock (used by purify_split_pages, but since we mock
 * purify_split_pages directly, this may not be needed) */
uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused,
                     status_$t *status) {
    (void)uid; (void)flags; (void)segment;
    (void)segment_list; (void)unused;
    *status = status_$ok;
    return 0;
}

/*
 * dir/alloc_split_page.c turns the 32-bit directory handle back into a
 * pointer with NAME_$HANDLE_TO_PTR (name/name.h).  On a 64-bit host that is
 * the registry in name/handle_map.c, so it is compiled in here and the tests
 * register mock_handle with NAME_$PTR_TO_HANDLE.
 */
#include "../../name/handle_map.c"

/* Pull in the implementation */
#include "../alloc_split_page.c"

/* ================================================================
 * Helper: reset all test state
 * ================================================================ */
static void reset_test_state(void) {
    init_page_pool();
    memset(mock_handle, 0, sizeof(mock_handle));
    crash_called = 0;
    uid_gen_called = 0;
    seg_map_called = 0;
    purify_called = 0;
    seg_map_status = 0;
    purify_status = 0;
    mock_seg_bitmap = 0xFFFFFFFF;
}

/* ================================================================
 * Test cases
 * ================================================================ */

/*
 * Test 1: Simple allocation with all pages in the gap.
 *
 * Setup: 8-page directory, pages 0-3 in use, pages 4-7 free.
 * max_depth=2, slot_idx=1, flag=0x00 (non-root).
 * pages_needed = 2-1 = 1, base_count = 2-1+1 = 2.
 * gap_count = 8-4 = 4, remaining_gap = 4-2 = 2.
 * Since pages_needed(1) <= remaining_gap(2), all from gap.
 *
 * Expected: split_pages[1]=6, split_pages[2]=7, split_pages[3]=4.
 */
TEST(simple_gap_allocation) {
    reset_test_state();
    init_handle(8);
    mark_page_used(0);
    mark_page_used(1);
    mark_page_used(2);
    mark_page_used(3);

    /* Set up B-tree path source pages with recognizable content */
    memset(page_pool[2], 0xAA, PAGE_SIZE);
    *(uint16_t *)page_pool[2] = 0x0001;  /* Keep it marked in-use */
    memset(page_pool[3], 0xBB, PAGE_SIZE);
    *(uint16_t *)page_pool[3] = 0x0001;

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 2;
    ctx.path_page[1] = 3;  /* Level 1 source page */
    ctx.path_page[2] = 2;  /* Level 2 (leaf) source page */

    status_$t status = status_$ok;
    dir_$alloc_split_page(&ctx, 0, 1, 0x12, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(2, ctx.page_count);
    /* Base pages: split_pages[1]=6, split_pages[2]=7 (next_page_start=6) */
    ASSERT_EQ(6, ctx.split_pages[1]);
    ASSERT_EQ(7, ctx.split_pages[2]);
    /* Extra gap page: split_pages[3]=4 (from gap at first_free_page=4) */
    ASSERT_EQ(4, ctx.split_pages[3]);
    ASSERT_EQ(1, uid_gen_called);
    ASSERT_EQ(1, purify_called);
    /* UID should be updated */
    ASSERT_EQ(0xDEADBEEF, ctx.dir_uid_high);
    ASSERT_EQ(0xCAFEBABE, ctx.dir_uid_low);
    /* Handle overflow flag set */
    ASSERT_EQ(0xFF, mock_handle[0x0E]);
    /* current_slot should be stored */
    ASSERT_EQ(1, ctx.current_slot);
}

/*
 * Test 2: Root split (flag=0xFF) allocates 2 extra pages.
 *
 * Setup: 8-page directory, pages 0-3 in use, pages 4-7 free.
 * max_depth=1, slot_idx=0, flag=0xFF.
 * pages_needed = 1-0+2 = 3, base_count = 1-0+1 = 2.
 * gap_count = 8-4 = 4, remaining_gap = 4-2 = 2.
 * Since pages_needed(3) > remaining_gap(2), goes to complex path.
 * But remaining_gap=2 > 0, so 2 gap pages used, still_needed=1.
 * Root page doesn't have segment map bit (0x0800), so skip seg scan.
 * still_needed=1, extend: split_pages[5] = next_page_start.
 */
TEST(root_split_extra_pages) {
    reset_test_state();
    init_handle(8);
    mark_page_used(0);
    mark_page_used(1);
    mark_page_used(2);
    mark_page_used(3);

    /* Source pages for copy */
    memset(page_pool[0], 0xCC, PAGE_SIZE);
    *(uint16_t *)page_pool[0] = 0x0001;
    memset(page_pool[1], 0xDD, PAGE_SIZE);
    *(uint16_t *)page_pool[1] = 0x0001;

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 1;
    ctx.path_page[0] = 1;
    ctx.path_page[1] = 0;

    status_$t status = status_$ok;
    dir_$alloc_split_page(&ctx, 0xFF, 0, 0x12, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(2, ctx.page_count);
    /* pages_needed = 3 (root split adds 2), base_count = 2 */
    /* Gap pages: split_pages[3]=4, split_pages[4]=5 (remaining_gap=2) */
    ASSERT_EQ(4, ctx.split_pages[3]);
    ASSERT_EQ(5, ctx.split_pages[4]);
    /* still_needed=1, extend: split_pages[5]=4 (next_page_start was 4, then +1=5?) */
    /* Actually: next_page_start = total_pages - base_count = 8-2 = 6
     * first_free_page = 4, gap_count = 4, remaining_gap = 4-2 = 2
     * pages_needed = 3 > remaining_gap = 2, so complex path:
     *   gap pages used: split_pages[3]=4, split_pages[4]=5
     *   still_needed = 3-2 = 1
     *   root page bit 11 not set, skip seg scan
     *   extend: split_pages[5] = next_page_start + 0 = 6
     *   next_page_start = 6 + 1 = 7
     * base pages: split_pages[1]=7, split_pages[2]=8
     * Hmm, but then base_count + next_page_start = 2 + 7 = 9, new size = 9*1024 */
    ASSERT_EQ(6, ctx.split_pages[5]);
    /* Base pages start at next_page_start=7 (after extension) */
    ASSERT_EQ(7, ctx.split_pages[1]);
    ASSERT_EQ(8, ctx.split_pages[2]);
    /* Directory size should be updated: (2 + 7) * 1024 = 9216 */
    ASSERT_EQ(9216, *(uint32_t *)(mock_handle + 0x10));
    ASSERT_EQ(0xFF, mock_handle[0x0E]);
}

/*
 * Test 3: Non-root split with exact gap match.
 *
 * Setup: 6-page directory, pages 0-3 in use, pages 4-5 free.
 * max_depth=2, slot_idx=1, flag=0x00.
 * pages_needed = 2-1 = 1, base_count = 2.
 * gap_count = 6-4 = 2, remaining_gap = 2-2 = 0.
 * Since pages_needed(1) > remaining_gap(0), complex path.
 * No gap pages to use, no segment map, extend by 1.
 */
TEST(extend_directory) {
    reset_test_state();
    init_handle(6);
    mark_page_used(0);
    mark_page_used(1);
    mark_page_used(2);
    mark_page_used(3);

    memset(page_pool[2], 0xEE, PAGE_SIZE);
    *(uint16_t *)page_pool[2] = 0x0001;
    memset(page_pool[3], 0xFF, PAGE_SIZE);
    *(uint16_t *)page_pool[3] = 0x0001;

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 2;
    ctx.path_page[1] = 3;
    ctx.path_page[2] = 2;

    status_$t status = status_$ok;
    dir_$alloc_split_page(&ctx, 0, 1, 0x12, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(2, ctx.page_count);
    /* next_page_start = 6-2 = 4, remaining_gap = 0
     * pages_needed = 1 > 0, complex path:
     *   no gap pages
     *   no segment map
     *   extend: split_pages[3] = next_page_start + 0 = 4
     *   next_page_start = 4 + 1 = 5
     * base pages: split_pages[1]=5, split_pages[2]=6 */
    ASSERT_EQ(4, ctx.split_pages[3]);
    ASSERT_EQ(5, ctx.split_pages[1]);
    ASSERT_EQ(6, ctx.split_pages[2]);
    /* Directory size: (2 + 5) * 1024 = 7168 */
    ASSERT_EQ(7168, *(uint32_t *)(mock_handle + 0x10));
}

/*
 * Test 4: Page copy updates UID and bit 4 flag.
 *
 * Verify that copied pages get the new UID and that bit 4
 * is set only when level==1 AND flag=0xFF (root split).
 */
TEST(page_copy_uid_and_flags) {
    reset_test_state();
    init_handle(8);
    mark_page_used(0);
    mark_page_used(1);
    mark_page_used(2);

    /* Set recognizable content in source pages */
    memset(page_pool[1], 0x11, PAGE_SIZE);
    *(uint16_t *)page_pool[1] = 0x0001;
    page_pool[1][0] = 0x41;  /* Some flags byte with bit 6 set */

    memset(page_pool[2], 0x22, PAGE_SIZE);
    *(uint16_t *)page_pool[2] = 0x0001;
    page_pool[2][0] = 0x01;

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 2;
    ctx.path_page[0] = 2;
    ctx.path_page[1] = 1;
    ctx.path_page[2] = 0;

    /* pages 0-2 in use, 3-7 free. base_count = 2-0+1 = 3, flag=0xFF
     * gap_count = 8-3 = 5, next_page_start = 8-3 = 5
     * pages_needed = 2-0+2 = 4, remaining_gap = 5-3 = 2
     * pages_needed(4) > remaining_gap(2), complex path */
    mark_page_used(0);

    status_$t status = status_$ok;
    dir_$alloc_split_page(&ctx, 0xFF, 0, 0x12, &status);

    ASSERT_EQ(status_$ok, status);

    /* Check that new UID was written to copied destination pages */
    /* The copy goes from max_depth=2 down to slot_idx=0, copying to
     * consecutive pages starting at next_page_start.
     * Destination pages: split_pages[1], split_pages[2], split_pages[3]
     * (the base pages). */
    int16_t dest_start = ctx.split_pages[1];
    /* Check UID in first destination page */
    uint8_t *dest0 = page_pool[dest_start];
    ASSERT_EQ(0xDEADBEEF, *(uint32_t *)(dest0 + 2));
    ASSERT_EQ(0xCAFEBABE, *(uint32_t *)(dest0 + 6));

    /* Check UID in second destination page */
    uint8_t *dest1 = page_pool[dest_start + 1];
    ASSERT_EQ(0xDEADBEEF, *(uint32_t *)(dest1 + 2));
    ASSERT_EQ(0xCAFEBABE, *(uint32_t *)(dest1 + 6));
}

/*
 * Test 5: No extra pages needed (flag=0, pages_needed=0).
 *
 * max_depth=1, slot_idx=1, flag=0: pages_needed = 0, base_count = 1.
 * This is the simplest case - just allocate 1 base page.
 */
TEST(zero_extra_pages) {
    reset_test_state();
    init_handle(4);
    mark_page_used(0);
    mark_page_used(1);

    memset(page_pool[1], 0x55, PAGE_SIZE);
    *(uint16_t *)page_pool[1] = 0x0001;

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 1;
    ctx.path_page[1] = 1;

    status_$t status = status_$ok;
    dir_$alloc_split_page(&ctx, 0, 1, 0x12, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, ctx.page_count);
    /* base_count=1, gap_count=4-2=2, next_page_start=4-1=3
     * pages_needed=0 <= remaining_gap=1, simple path
     * No extra pages stored. Base: split_pages[1]=3 */
    ASSERT_EQ(3, ctx.split_pages[1]);
    /* idx_base should be page_data + base_offset */
    ASSERT_EQ(ctx.page_data + 0x12, ctx.idx_base);
}

/*
 * Test 6: Segment map scan finds free pages.
 *
 * Setup: 10-page directory, pages 0,1,4 in use, pages 2,3 free (in seg map),
 * pages 5-9 free. Root page has bit 11 set (segment map present).
 * Bitmap for segment 0: bits 0,1,4 set, bits 2,3 clear.
 */
TEST(seg_map_scan) {
    reset_test_state();
    init_handle(10);
    mark_page_used(0);
    mark_page_used(1);
    /* pages 2,3 are free (not marked) */
    mark_page_used(4);
    /* pages 5-9 are free (not marked) */

    /* Set bit 11 (0x0800) on root page to indicate segment map exists */
    *(uint16_t *)page_pool[0] |= 0x0800;

    /* Segment 0 bitmap: pages 0,1,4 in use (bits 0,1,4 set) */
    mock_seg_bitmap = (1u << 0) | (1u << 1) | (1u << 4);

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 3;
    ctx.path_page[1] = 4;
    ctx.path_page[2] = 1;
    ctx.path_page[3] = 0;

    /* slot_idx=1, flag=0: pages_needed = 3-1 = 2, base_count = 3
     * first_free_page = 5 (pages 0,1,4 used, scan backward from 9,
     *   9=empty, 8=empty, ..., 5=empty, 4=used → first_free=5)
     * gap_count = 10-5 = 5, remaining_gap = 5-3 = 2
     * pages_needed(2) <= remaining_gap(2), simple path! */
    status_$t status = status_$ok;
    dir_$alloc_split_page(&ctx, 0, 1, 0x12, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(3, ctx.page_count);
    /* Segment map not scanned since simple gap path */
    ASSERT_EQ(0, seg_map_called);
    /* Extra pages from gap: split_pages[4]=5, split_pages[5]=6 */
    ASSERT_EQ(5, ctx.split_pages[4]);
    ASSERT_EQ(6, ctx.split_pages[5]);
}

/*
 * Test 7: Purify failure propagates status.
 */
TEST(purify_failure) {
    reset_test_state();
    init_handle(8);
    mark_page_used(0);
    mark_page_used(1);

    memset(page_pool[1], 0x77, PAGE_SIZE);
    *(uint16_t *)page_pool[1] = 0x0001;

    purify_status = 0x00030001;  /* Some error */

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 1;
    ctx.path_page[1] = 1;

    status_$t status = status_$ok;
    dir_$alloc_split_page(&ctx, 0, 1, 0x12, &status);

    /* Status should be the error from purify */
    ASSERT_EQ(0x00030001, status);
    /* Handle flag should NOT be set on failure */
    ASSERT_EQ(0, mock_handle[0x0E]);
}

/*
 * Test 8: Segment map scan actually used (no gap pages available).
 *
 * Setup: 5-page directory, pages 0,1,2,3,4 all in use.
 * Root has bit 11 set. Bitmap shows page 2 as free.
 * max_depth=1, slot_idx=0, flag=0xFF.
 * pages_needed = 1-0+2 = 3, base_count = 2.
 * first_free_page = 5 (all pages in use).
 * gap_count = 5-5 = 0, remaining_gap = 0 (gap < base_count).
 * next_page_start = 5 (= first_free_page).
 * Complex path: no gap pages, scan seg map for 3 free pages.
 * Bitmap: bit 2 clear (page 2 free), rest set.
 * Gets 1 page from seg map, still need 2, extend.
 */
TEST(seg_map_scan_with_extension) {
    reset_test_state();
    init_handle(5);
    mark_page_used(0);
    mark_page_used(1);
    mark_page_used(2);
    mark_page_used(3);
    mark_page_used(4);

    /* Set bit 11 on root page */
    *(uint16_t *)page_pool[0] |= 0x0800;

    /* Bitmap: all bits set except bit 2 (page 2 is free) */
    mock_seg_bitmap = 0xFFFFFFFF & ~(1u << 2);

    /* Source pages */
    memset(page_pool[3], 0xAB, PAGE_SIZE);
    *(uint16_t *)page_pool[3] = 0x0001;
    memset(page_pool[4], 0xCD, PAGE_SIZE);
    *(uint16_t *)page_pool[4] = 0x0001;

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 1;
    ctx.path_page[0] = 4;
    ctx.path_page[1] = 3;

    status_$t status = status_$ok;
    dir_$alloc_split_page(&ctx, 0xFF, 0, 0x12, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(2, ctx.page_count);
    ASSERT_EQ(1, seg_map_called);

    /* Seg map found page 2 free: stored in split_pages at position 3
     * (base_count=2, starting from split_idx=2, then ++3) */
    ASSERT_EQ(2, ctx.split_pages[3]);
    /* Still needed 2 more, extended: split_pages[4]=5, split_pages[5]=6 */
    /* Actually: pages_needed=3, remaining_gap=0,
     * seg map finds 1 free page (page 2), still_needed=2
     * extend: split_pages[4]=5 (next_page_start=5), split_pages[5]=6
     * next_page_start becomes 5+2=7
     * base pages: split_pages[1]=7, split_pages[2]=8 */
    ASSERT_EQ(5, ctx.split_pages[4]);
    ASSERT_EQ(6, ctx.split_pages[5]);
    ASSERT_EQ(7, ctx.split_pages[1]);
    ASSERT_EQ(8, ctx.split_pages[2]);
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    printf("dir_$alloc_split_page tests:\n");

    RUN_TEST(simple_gap_allocation);
    RUN_TEST(root_split_extra_pages);
    RUN_TEST(extend_directory);
    RUN_TEST(page_copy_uid_and_flags);
    RUN_TEST(zero_extra_pages);
    RUN_TEST(seg_map_scan);
    RUN_TEST(purify_failure);
    RUN_TEST(seg_map_scan_with_extension);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
