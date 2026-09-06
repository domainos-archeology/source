/*
 * dir/test/test_insert_entry.c - Unit tests for dir_$insert_entry (0x00E4F3BA)
 *
 * The real dir/insert_entry.c is #included below and driven through mocks of
 * its helpers.  The assertions target the defect the 2026-09-06 fidelity audit
 * found in this function: the page header word at page+0x0A (page_no, zero on
 * the root page) was read once at 0xE4F40A and then reused, but the original
 * re-reads it at 0xE4F548 and 0xE4F600, after dir_$compact_page_entries has
 * had the chance to rewrite the page in place.
 *
 * dir_page_hdr_t's offsets are checked here too, because the _Static_asserts
 * that guard them only compile under ARCH_M68K.
 */

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                    */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* Globals and mocks                                                    */
/* ------------------------------------------------------------------ */

/*
 * DIR_$NAME_OFFSET_TABLE (0xE7FC00 + type*2, reached through A5 in the
 * original): the fixed part of an entry, by entry type.
 */
int16_t DIR_$NAME_OFFSET_TABLE[8] = { 0, 4, 16, 20, 12, 0, 0, 0 };

char Naming_bad_request_header_ver_err;

#define DIR_PAGE_BYTES  0x400
static uint8_t page_a[DIR_PAGE_BYTES];      /* the page for level 1 */
static uint8_t page_b[DIR_PAGE_BYTES];      /* the page for level 0 (parent) */

static int      mock_map_calls;
static int16_t  mock_map_last_page;

void *dir_$map_page(void *handle, int16_t page_idx)
{
    (void)handle;
    mock_map_calls++;
    mock_map_last_page = page_idx;
    return (page_idx == 1) ? page_a : page_b;
}

/* dir_$compact_page_entries: the test decides what the "rewritten" page
 * looks like afterwards. */
static int      mock_compact_calls;
static int      mock_compact_sets_page_no;
static uint16_t mock_compact_new_page_no;
static int      mock_compact_frees_space;

void dir_$compact_page_entries(dir_insert_ctx_t *ctx)
{
    dir_page_hdr_t *hdr = (dir_page_hdr_t *)ctx->page_data;

    mock_compact_calls++;
    if (mock_compact_sets_page_no) {
        hdr->page_no = mock_compact_new_page_no;
    }
    if (mock_compact_frees_space) {
        hdr->heap_base = DIR_PAGE_BYTES;
    }
}

static int      mock_alloc_split_calls;
static uint8_t  mock_alloc_split_flag;
static status_$t mock_alloc_split_status;

void dir_$alloc_split_page(dir_insert_ctx_t *ctx, uint8_t flag,
                           int16_t slot_idx, int16_t base_offset,
                           status_$t *status_ret)
{
    (void)ctx; (void)slot_idx; (void)base_offset;
    mock_alloc_split_calls++;
    mock_alloc_split_flag = flag;
    *status_ret = mock_alloc_split_status;
}

static uint16_t mock_entry_size;
uint16_t dir_$calc_entry_size(uint8_t *entry) { (void)entry; return mock_entry_size; }

static int mock_move_calls;
void dir_$move_entries_to_page(dir_insert_ctx_t *ctx, int16_t from_idx, int16_t to_idx)
{ (void)ctx; (void)from_idx; (void)to_idx; mock_move_calls++; }

static int mock_write_calls;
void dir_$write_entry_to_page(dir_insert_ctx_t *ctx, uint8_t flag,
                              uint8_t **page_ptr_ref, int16_t count,
                              int16_t name_len, uint16_t aligned_size,
                              uint32_t src_name_loc)
{ (void)ctx; (void)flag; (void)page_ptr_ref; (void)count; (void)name_len;
  (void)aligned_size; (void)src_name_loc; mock_write_calls++; }

static int mock_finalize_calls;
void dir_$finalize_split(dir_insert_ctx_t *ctx, status_$t *status_ret)
{ (void)ctx; mock_finalize_calls++; *status_ret = status_$ok; }

void dir_$remove_entry(void *handle, void *name, int16_t name_len,
                       int16_t op_type, void *uid_ret, status_$t *status_ret)
{ (void)handle; (void)name; (void)name_len; (void)op_type; (void)uid_ret;
  *status_ret = status_$ok; }

static int mock_wire_calls;
void DIR_$WIRE_PAGE(void *handle, void *page_data)
{ (void)handle; (void)page_data; mock_wire_calls++; }

static int mock_release_calls;
void dir_$release_wire(void *handle) { (void)handle; mock_release_calls++; }

static int mock_crash_calls;
void CRASH_SYSTEM(const status_$t *status_p) { (void)status_p; mock_crash_calls++; }

status_$t FIM_$CLEANUP(void *handler) { (void)handler; return 0; }
void FIM_$RLS_CLEANUP(void *cleanup_data) { (void)cleanup_data; }
void FIM_$SIGNAL(status_$t status) { (void)status; }

/* ------------------------------------------------------------------ */
/* Code under test                                                      */
/* ------------------------------------------------------------------ */

#include "../insert_entry.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

static dir_insert_ctx_t ctx;

/*
 * Build a page that is out of space and marked reclaimable, so that
 * dir_$insert_entry wires it and calls dir_$compact_page_entries.
 *
 * kind byte 0x20 => page kind 0 (leaf) and bit 13 of the flags word set,
 * which is the "has dead entries" bit that btst #13 tests at 0xE4F4F0.
 */
static void build_full_leaf(uint8_t *page, uint16_t page_no,
                            uint16_t index_end, uint16_t heap_base,
                            int reclaimable)
{
    dir_page_hdr_t *hdr = (dir_page_hdr_t *)page;

    memset(page, 0, DIR_PAGE_BYTES);
    hdr->kind = reclaimable ? 0x20 : 0x00;
    hdr->version = 5;
    hdr->page_no = page_no;
    hdr->next_page = 0xFFFF;
    hdr->index_end = index_end;
    hdr->heap_base = heap_base;
}

static void reset(void)
{
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = 0x1234;                /* only ever handed to mocks */
    ctx.entry_type = 2;                 /* fixed part = 16 bytes */
    ctx.overflow_page = -1;
    ctx.link_len = 0;
    ctx.max_depth = 8;
    ctx.path_page[1] = 1;               /* level 1 -> page_a */
    ctx.path_page[0] = 0;               /* level 0 -> page_b */
    ctx.path_entry[1] = 0;
    ctx.path_entry[0] = 0;

    mock_map_calls = 0;
    mock_compact_calls = 0;
    mock_compact_sets_page_no = 0;
    mock_compact_new_page_no = 0;
    mock_compact_frees_space = 0;
    mock_alloc_split_calls = 0;
    mock_alloc_split_status = status_$ok;
    mock_move_calls = 0;
    mock_write_calls = 0;
    mock_finalize_calls = 0;
    mock_wire_calls = 0;
    mock_release_calls = 0;
    mock_crash_calls = 0;
    mock_entry_size = 0x1F7;            /* one iteration ends the split search */

    /* Level 1: a full, reclaimable, NON-root page with four index slots. */
    build_full_leaf(page_a, /*page_no*/ 5, /*index_end*/ 0x1A,
                    /*heap_base*/ 0x1C, /*reclaimable*/ 1);
    *(uint16_t *)(page_a + 0x12) = 0x100;
    *(uint16_t *)(page_a + 0x14) = 0x120;
    page_a[0x120] = 2;                  /* entry type for the recursive param */
    page_a[0x121] = 4;                  /* name length */

    /* Level 0: a full ROOT page that is not reclaimable. */
    build_full_leaf(page_b, /*page_no*/ 0, /*index_end*/ 0x12,
                    /*heap_base*/ 0x14, /*reclaimable*/ 0);
    *(int16_t *)(page_b + DIR_PAGE_ROOT_AREA_LEN) = 0;
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/* The recovered header layout, checked at run time because the
 * _Static_asserts in dir_internal.h only compile under ARCH_M68K. */
TEST(page_header_layout)
{
    ASSERT_EQ(0x00, offsetof(dir_page_hdr_t, kind));
    ASSERT_EQ(0x01, offsetof(dir_page_hdr_t, version));
    ASSERT_EQ(0x02, offsetof(dir_page_hdr_t, dir_uid_high));
    ASSERT_EQ(0x06, offsetof(dir_page_hdr_t, dir_uid_low));
    ASSERT_EQ(0x0A, offsetof(dir_page_hdr_t, page_no));
    ASSERT_EQ(0x0C, offsetof(dir_page_hdr_t, next_page));
    ASSERT_EQ(0x0E, offsetof(dir_page_hdr_t, index_end));
    ASSERT_EQ(0x10, offsetof(dir_page_hdr_t, heap_base));
    ASSERT_EQ(DIR_PAGE_HDR_SIZE, sizeof(dir_page_hdr_t));
}

/*
 * The page starts out as a non-root page (page_no == 5) but compaction
 * rewrites it into the root page (page_no == 0).  The original re-reads
 * page+0x0A at 0xE4F548, so it must take the root-split path and, with
 * max_depth already at 8, fail with status_$directory_is_full without
 * recursing.  Code that cached the first read would instead have gone down
 * the non-root path and recursed into level 0.
 */
TEST(page_no_is_reread_after_compaction)
{
    status_$t status = 0xdeadbeef;

    reset();
    mock_compact_sets_page_no = 1;
    mock_compact_new_page_no = 0;       /* compaction turned it into the root */

    dir_$insert_entry(&ctx, 1, 0, 4, &status);

    ASSERT_EQ(1, mock_compact_calls);
    ASSERT_EQ(status_$directory_is_full, status);
    /* no recursion: dir_$map_page ran once, for level 1 only */
    ASSERT_EQ(1, mock_map_calls);
    ASSERT_EQ(0, mock_alloc_split_calls);
    ASSERT_EQ(0, mock_crash_calls);
}

/*
 * The control case: compaction leaves page_no alone, so the non-root split
 * path runs and dir_$insert_entry recurses into level 0.  That level is the
 * root page and max_depth is 8, so the recursion returns
 * status_$directory_is_full and the outer call passes it straight back.
 * The second dir_$map_page call is the proof that the recursion happened.
 */
TEST(non_root_page_still_recurses)
{
    status_$t status = 0xdeadbeef;

    reset();
    mock_compact_sets_page_no = 0;      /* page stays page_no == 5 */

    dir_$insert_entry(&ctx, 1, 0, 4, &status);

    ASSERT_EQ(1, mock_compact_calls);
    ASSERT_EQ(2, mock_map_calls);       /* level 1, then level 0 */
    ASSERT_EQ(0, mock_map_last_page);   /* the recursive call mapped level 0 */
    ASSERT_EQ(status_$directory_is_full, status);
    ASSERT_EQ(0, mock_crash_calls);
}

/*
 * The free-space recomputation at 0xE4F512 reads the header back too: when
 * compaction reclaims enough space the simple insertion path runs instead of
 * a split.  The page here is an interior page (kind 1), so the simple path
 * starts with dir_$alloc_split_page(flag = 0) at 0xE4FCD6; failing that call
 * returns before the code dereferences ctx->handle, which a 64-bit host
 * cannot do (see source-qu3v).
 */
TEST(free_space_is_reread_after_compaction)
{
    status_$t status = 0xdeadbeef;

    reset();
    /* kind byte 0x60: page kind 1 (interior) and flags-word bit 13 set. */
    ((dir_page_hdr_t *)page_a)->kind = 0x60;
    mock_compact_frees_space = 1;       /* heap_base -> 0x400 */
    mock_alloc_split_status = 0x000E0002;

    dir_$insert_entry(&ctx, 1, 0, 4, &status);

    ASSERT_EQ(1, mock_compact_calls);
    ASSERT_EQ(1, mock_alloc_split_calls);
    ASSERT_EQ(0, mock_alloc_split_flag);    /* the simple path passes 0 */
    ASSERT_EQ(0, mock_write_calls);
    ASSERT_EQ(0x000E0002, status);
}

/*
 * A page that is not marked reclaimable is never compacted; it goes straight
 * to the split path, where page+0x0A is read at 0xE4F548 and 0xE4F600.
 */
TEST(non_reclaimable_page_skips_compaction)
{
    status_$t status = 0xdeadbeef;

    reset();
    build_full_leaf(page_a, 0, 0x12, 0x14, /*reclaimable*/ 0);
    *(int16_t *)(page_a + DIR_PAGE_ROOT_AREA_LEN) = 0;

    dir_$insert_entry(&ctx, 1, 0, 4, &status);

    ASSERT_EQ(0, mock_compact_calls);
    ASSERT_EQ(status_$directory_is_full, status);
    ASSERT_EQ(1, mock_map_calls);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("dir_$insert_entry tests\n");
    RUN_TEST(page_header_layout);
    RUN_TEST(page_no_is_reread_after_compaction);
    RUN_TEST(non_root_page_still_recurses);
    RUN_TEST(free_space_is_reread_after_compaction);
    RUN_TEST(non_reclaimable_page_skips_compaction);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
