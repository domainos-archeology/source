/*
 * dir/test/test_remove_entry.c - unit tests for dir_$remove_entry
 * (0x00E50FC8) and its flattened nested subprocedure (0x00E50D5E)
 *
 * Bead source-05f8: the image passes its own frame to 0x00E50D5E as a static
 * link (`movea.l A6,A1` at 0x00E5108E) and the subprocedure sets the
 * did_truncate byte at A6-0x60 (`st (-0x60,A3)` at 0x00E50F94).  Everything
 * from 0x00E5110A to 0x00E511D0 - the backwards scan for the last live page,
 * the AST_$TRUNCATE and the multi-page flag in page 0 - runs only when that
 * byte is negative, so with the flag unreachable the whole block was dead.
 */

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* Globals and mocks                                                    */
/* ------------------------------------------------------------------ */

status_$t Naming_bad_request_header_ver_err = 0x000E0025;

static int              crash_calls;
static const status_$t *crash_arg;
static jmp_buf          crash_jmp;
void CRASH_SYSTEM(const status_$t *st)
{
    crash_calls++;
    crash_arg = st;
    longjmp(crash_jmp, 1);
}

/*
 * The test directory: TEST_MAX_PAGES pages of DIR_PAGE_SIZE, plus a
 * "handle" whose first eight bytes are the directory UID and whose longword
 * at +0x10 is the byte size (that is what 0x00E51118 and 0x00E511B8 read).
 */
#define TEST_MAX_PAGES 8
static uint8_t pages[TEST_MAX_PAGES][DIR_PAGE_SIZE];

typedef struct test_handle_t {
    uid_t    uid;           /* +0x00 */
    uint8_t  pad[8];        /* +0x08 */
    uint32_t size;          /* +0x10 */
} test_handle_t;
static test_handle_t handle;

static int     mp_calls;
static int16_t mp_last;
void *dir_$map_page(void *h, int16_t page_idx)
{
    (void)h;
    mp_calls++;
    mp_last = page_idx;
    if (page_idx < 0 || page_idx >= TEST_MAX_PAGES) {
        return pages[0];
    }
    return pages[page_idx];
}

static int wire_calls, unwire_calls;
void DIR_$WIRE_PAGE(void *h, void *page_data)
{
    (void)h; (void)page_data; wire_calls++;
}
void dir_$release_wire(void *h) { (void)h; unwire_calls++; }

static int      purify_calls;
static uint32_t purify_seg_seen;
static uint16_t purify_flags_seen;
uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused,
                     status_$t *status)
{
    purify_calls++;
    purify_flags_seen = flags;
    purify_seg_seen = *segment_list;
    (void)uid; (void)segment; (void)unused;
    *status = status_$ok;
    return 0;
}

static int      inval_calls;
static uint32_t inval_page_seen;
void AST_$INVALIDATE(uid_t *uid, uint32_t start_page, uint32_t count,
                     boolean flags, status_$t *status)
{
    inval_calls++;
    inval_page_seen = start_page;
    (void)uid; (void)count; (void)flags;
    *status = status_$ok;
}

static int       trunc_calls;
static uint32_t  trunc_size_seen;
static status_$t trunc_status;
void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   boolean *result, status_$t *status)
{
    trunc_calls++;
    trunc_size_seen = new_size;
    (void)uid; (void)flags;
    *result = 0;
    *status = trunc_status;
}

/*
 * dir_$find_entry mock.  It reports a single-level path (level 1) into the
 * caller's buffer and hands back a pointer to a synthetic entry.
 */
static char      fe_result;
static uint8_t   fe_entry[16];
static uint16_t  fe_path_page;
static uint16_t  fe_path_slot;
static int16_t   fe_depth;
static int16_t   fe_capacity_seen;
char dir_$find_entry(void *h, void *name, int16_t name_len,
                     int16_t flags, void **entry_ret,
                     void *extra, int16_t *depth_ret)
{
    dir_$page_path_t *path = (dir_$page_path_t *)extra;
    (void)h; (void)name; (void)name_len;
    fe_capacity_seen = flags;
    path[0].page_no = fe_path_page;
    path[0].entry_idx = fe_path_slot;
    *entry_ret = fe_entry;
    *depth_ret = fe_depth;
    return fe_result;
}

#include "../remove_entry.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

static status_$t st;
static uid_t     uid_out;
static char      the_name[] = "E";

/*
 * Build a leaf page holding `n` entries.  Header: kind 0x40 (leaf), page_no,
 * index_end = 0x12 + n*2, heap growing down from 0x400.  Entry k's index word
 * points at 0x400 - (k+1)*0x10.
 */
static void make_leaf(int idx, uint16_t page_no, int n)
{
    dir_page_hdr_t *h = (dir_page_hdr_t *)pages[idx];
    uint16_t *tab = (uint16_t *)(pages[idx] + DIR_PAGE_HDR_SIZE);
    int k;

    memset(pages[idx], 0, DIR_PAGE_SIZE);
    h->kind = 0x40;                 /* kind field == 1 => leaf */
    h->version = 5;
    h->page_no = page_no;
    h->next_page = 0xFFFF;
    h->index_end = (uint16_t)(DIR_PAGE_HDR_SIZE + n * 2);
    h->heap_base = (uint16_t)(DIR_PAGE_SIZE - n * 0x10);
    for (k = 0; k < n; k++) {
        tab[k] = (uint16_t)(DIR_PAGE_SIZE - (k + 1) * 0x10);
    }
}

static void reset(void)
{
    memset(pages, 0, sizeof(pages));
    memset(&handle, 0, sizeof(handle));
    handle.uid.high = 0xAAAAAAAAu;
    handle.uid.low  = 0xBBBBBBBBu;
    handle.size = 4 * DIR_PAGE_SIZE;
    memset(fe_entry, 0, sizeof(fe_entry));
    fe_entry[0] = 2;                /* a plain object entry */
    *(uint32_t *)(fe_entry + 4) = 0x12345678u;
    *(uint32_t *)(fe_entry + 8) = 0x9ABCDEF0u;
    fe_result = (char)0xFF;         /* found */
    fe_path_page = 3;
    fe_path_slot = 1;
    fe_depth = 1;
    fe_capacity_seen = 0;
    crash_calls = 0; crash_arg = NULL;
    mp_calls = 0; mp_last = -1;
    wire_calls = 0; unwire_calls = 0;
    purify_calls = 0; purify_seg_seen = 0xFFFFFFFFu; purify_flags_seen = 0;
    inval_calls = 0; inval_page_seen = 0xFFFFFFFFu;
    trunc_calls = 0; trunc_size_seen = 0xFFFFFFFFu; trunc_status = status_$ok;
    st = 0x5A5A5A5A;
    uid_out.high = 0; uid_out.low = 0;
    /* Two live pages beyond the root, one dead tail page. */
    make_leaf(0, 0, 2);
    make_leaf(1, 1, 2);
    make_leaf(2, 2, 2);
    make_leaf(3, 3, 2);
}

static int run(int16_t op_type)
{
    if (setjmp(crash_jmp)) {
        return 1;
    }
    dir_$remove_entry(&handle, the_name, 1, op_type, &uid_out, &st);
    return 0;
}

/* 0x00E50FDC: the word 8 is the path buffer's capacity. */
TEST(find_entry_is_told_the_path_buffer_holds_eight_levels)
{
    reset();
    ASSERT_EQ(0, run(0));
    ASSERT_EQ(8, fe_capacity_seen);
}

/* 0x00E51008 */
TEST(a_missing_entry_is_name_not_found)
{
    reset();
    fe_result = 0;
    ASSERT_EQ(0, run(0));
    ASSERT_EQ(status_$naming_name_not_found, st);
    ASSERT_EQ(0, wire_calls);
}

/* 0x00E51012: types 2 and 3 hand the UID at entry+4 back. */
TEST(a_type_2_entry_returns_its_uid)
{
    reset();
    ASSERT_EQ(0, run(0));
    ASSERT_EQ(0x12345678u, uid_out.high);
    ASSERT_EQ(0x9ABCDEF0u, uid_out.low);
}

/* 0x00E5104C: op_type 4 on a non-link entry */
TEST(asking_to_drop_a_link_on_a_plain_entry_fails)
{
    reset();
    ASSERT_EQ(0, run(4));
    ASSERT_EQ(status_$naming_not_a_link, st);
    ASSERT_EQ(0, wire_calls);
}

/* 0x00E51060: any other op_type on a link entry */
TEST(a_non_link_operation_on_a_link_entry_fails)
{
    reset();
    fe_entry[0] = 4;
    ASSERT_EQ(0, run(2));
    ASSERT_EQ(status_$naming_invalid_link_operation, st);
    ASSERT_EQ(0, wire_calls);
}

/*
 * The plain removal path: the leaf page loses one index slot, gets its
 * reclaim bit set, and - because the page is a leaf - the nested procedure
 * sets did_truncate, so the tail block at 0x00E5110A runs.
 */
TEST(removing_from_a_leaf_shrinks_the_index_and_sets_the_reclaim_bit)
{
    dir_page_hdr_t *p;
    reset();
    handle.size = 2 * DIR_PAGE_SIZE;
    make_leaf(1, 1, 3);
    fe_path_page = 1;
    fe_path_slot = 2;

    ASSERT_EQ(0, run(0));
    ASSERT_EQ(status_$ok, st);
    p = (dir_page_hdr_t *)pages[1];
    ASSERT_EQ(DIR_PAGE_HDR_SIZE + 2 * 2, p->index_end);
    ASSERT_EQ(0x20, p->kind & 0x20);
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ(1, unwire_calls);
    /* 0x00E50F94/0x00E50F9E: the leaf sets did_truncate and purifies. */
    ASSERT_EQ(1, purify_calls);
    ASSERT_EQ(0x0012, purify_flags_seen);
    ASSERT_EQ(1, purify_seg_seen);          /* the page's own page_no */
    /* 0x00E51168: shrinking by less than 0x1000 is not worth a truncate. */
    ASSERT_EQ(0, trunc_calls);
}

/*
 * did_truncate reaches the caller: 0x00E5110A-0x00E511D0 walks back from the
 * last page, finds page 2 as the last live one, and truncates to
 * (2+1)*0x400 + 0x400 == 0x1000.  Page 0's bit 3 then records "more than one
 * page".
 */
TEST(the_did_truncate_flag_drives_the_backwards_scan_and_truncate)
{
    reset();
    handle.size = 8 * DIR_PAGE_SIZE;
    make_leaf(1, 1, 3);
    fe_path_page = 1;
    fe_path_slot = 2;
    /* Pages 3..7 are dead (all-zero header word). */
    memset(pages[3], 0, DIR_PAGE_SIZE);
    memset(pages[4], 0, DIR_PAGE_SIZE);
    memset(pages[5], 0, DIR_PAGE_SIZE);
    memset(pages[6], 0, DIR_PAGE_SIZE);
    memset(pages[7], 0, DIR_PAGE_SIZE);

    ASSERT_EQ(0, run(0));
    ASSERT_EQ(1, trunc_calls);
    ASSERT_EQ(0x1000u, trunc_size_seen);
    ASSERT_EQ(0x1000u, handle.size);
    ASSERT_EQ(0x08, pages[0][0] & 0x08);
}

/* 0x00E51196: a failed AST_$TRUNCATE leaves the recorded size alone. */
TEST(a_failed_truncate_does_not_update_the_size)
{
    reset();
    handle.size = 8 * DIR_PAGE_SIZE;
    trunc_status = status_$naming_internal_error;
    make_leaf(1, 1, 3);
    fe_path_page = 1;
    fe_path_slot = 2;
    memset(pages[3], 0, DIR_PAGE_SIZE);
    memset(pages[4], 0, DIR_PAGE_SIZE);
    memset(pages[5], 0, DIR_PAGE_SIZE);
    memset(pages[6], 0, DIR_PAGE_SIZE);
    memset(pages[7], 0, DIR_PAGE_SIZE);

    ASSERT_EQ(0, run(0));
    ASSERT_EQ(1, trunc_calls);
    ASSERT_EQ(8u * DIR_PAGE_SIZE, handle.size);
}

/*
 * 0x00E51164: a one-page directory always truncates (new_size == 0x400), and
 * page 0's bit 3 is cleared.
 */
TEST(a_single_page_directory_truncates_and_clears_the_multipage_bit)
{
    reset();
    handle.size = DIR_PAGE_SIZE;
    make_leaf(1, 1, 3);
    pages[0][0] |= 0x08;
    fe_path_page = 1;
    fe_path_slot = 2;

    ASSERT_EQ(0, run(0));
    ASSERT_EQ(1, trunc_calls);
    ASSERT_EQ((uint32_t)DIR_PAGE_SIZE, trunc_size_seen);
    ASSERT_EQ(0, pages[0][0] & 0x08);
}

/*
 * 0x00E5113C: the backwards scan crashes if it runs off the front of the
 * directory without finding a page whose header word is nonzero.
 */
TEST(a_scan_that_runs_off_the_front_crashes)
{
    reset();
    /* The removal happens on page 1, so page 0 is left blank. */
    handle.size = DIR_PAGE_SIZE;        /* last_page == 0 */
    make_leaf(1, 1, 3);
    memset(pages[0], 0, DIR_PAGE_SIZE);
    fe_path_page = 1;
    fe_path_slot = 2;

    ASSERT_EQ(1, run(0));
    ASSERT_EQ(1, crash_calls);
    ASSERT_TRUE(crash_arg == &Naming_bad_request_header_ver_err);
    ASSERT_EQ(0, trunc_calls);
}

/*
 * The nested procedure only sets did_truncate for a LEAF page (0x00E50F8E:
 * `tst.b (-0x62,A3)` / bpl), so an interior page leaves the tail block alone.
 */
TEST(an_interior_page_neither_purifies_nor_truncates)
{
    reset();
    handle.size = 8 * DIR_PAGE_SIZE;
    make_leaf(1, 1, 3);
    ((dir_page_hdr_t *)pages[1])->kind = 0x80;      /* kind field == 2 */
    fe_path_page = 1;
    fe_path_slot = 2;

    ASSERT_EQ(0, run(0));
    ASSERT_EQ(0, purify_calls);
    ASSERT_EQ(0, trunc_calls);
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ(1, unwire_calls);
    ASSERT_EQ(status_$ok, st);
}

/*
 * 0x00E50D90: on the ROOT page (page_no == 0) the header size is
 * 0x12 plus the root-area length word at +0x14, so the index table starts
 * past the root area.
 */
TEST(the_root_pages_index_table_starts_past_its_root_area)
{
    dir_page_hdr_t *p;
    uint16_t *tab;
    const uint16_t root_area = 0x30;
    int k;

    reset();
    handle.size = 2 * DIR_PAGE_SIZE;
    memset(pages[0], 0, DIR_PAGE_SIZE);
    p = (dir_page_hdr_t *)pages[0];
    p->kind = 0x40;
    p->version = 5;
    p->page_no = 0;
    p->next_page = 0xFFFF;
    *(uint16_t *)(pages[0] + DIR_PAGE_ROOT_AREA_LEN) = root_area;
    p->index_end = (uint16_t)(DIR_PAGE_HDR_SIZE + root_area + 3 * 2);
    p->heap_base = (uint16_t)(DIR_PAGE_SIZE - 3 * 0x10);
    tab = (uint16_t *)(pages[0] + DIR_PAGE_HDR_SIZE + root_area);
    for (k = 0; k < 3; k++) {
        tab[k] = (uint16_t)(DIR_PAGE_SIZE - (k + 1) * 0x10);
    }
    fe_path_page = 0;
    fe_path_slot = 2;

    ASSERT_EQ(0, run(0));
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(DIR_PAGE_HDR_SIZE + root_area + 2 * 2, p->index_end);
    /* Entry 2 (index 1) was marked dead and entry 3 shifted down over it. */
    ASSERT_EQ(0x80, pages[0][DIR_PAGE_SIZE - 2 * 0x10] & 0x80);
    ASSERT_EQ((uint16_t)(DIR_PAGE_SIZE - 3 * 0x10), tab[1]);
}

int main(void)
{
    printf("dir_$remove_entry (0x00E50FC8) tests\n");

    RUN_TEST(find_entry_is_told_the_path_buffer_holds_eight_levels);
    RUN_TEST(a_missing_entry_is_name_not_found);
    RUN_TEST(a_type_2_entry_returns_its_uid);
    RUN_TEST(asking_to_drop_a_link_on_a_plain_entry_fails);
    RUN_TEST(a_non_link_operation_on_a_link_entry_fails);
    RUN_TEST(removing_from_a_leaf_shrinks_the_index_and_sets_the_reclaim_bit);
    RUN_TEST(the_did_truncate_flag_drives_the_backwards_scan_and_truncate);
    RUN_TEST(a_failed_truncate_does_not_update_the_size);
    RUN_TEST(a_single_page_directory_truncates_and_clears_the_multipage_bit);
    RUN_TEST(the_root_pages_index_table_starts_past_its_root_area);
    RUN_TEST(a_scan_that_runs_off_the_front_crashes);
    RUN_TEST(an_interior_page_neither_purifies_nor_truncates);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
