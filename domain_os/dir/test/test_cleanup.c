/*
 * dir/test/test_cleanup.c - unit tests for DIR_$CLEANUP (0x00E53578)
 *
 * dir/cleanup.c is #included below and driven through mocks of every routine
 * it calls, over a host stand-in for the DIR module block at A5 = 0x00E7DC00.
 * The behaviours covered are the ones bead source-f13d reported missing: the
 * per-slot owner test, the split_busy short path, the backwards page scan and
 * the DIR_$LINK_BUF_MUTEX handover.
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
/* Globals the .c under test reads                                      */
/* ------------------------------------------------------------------ */

uint16_t PROC1_$CURRENT;
uid_t    UID_$NIL = { 0, 0 };

ml_$exclusion_t DIR_$LINK_BUF_MUTEX;
status_$t Naming_bad_request_header_ver_err = 0x000E0025;
status_$t *const DIR_$CRASH_STATUS = &Naming_bad_request_header_ver_err;

/*
 * The DIR module block.  The image's A5 is 0x00E7DC00 and the routine reaches
 * 0x0000..0x2124 above it plus -0x8..-0x1 below it, so the buffer carries a
 * head of 8 bytes and the base pointer sits inside it.
 */
static uint8_t block_store[8 + 0x2200];
#define A5_AREA (block_store + 8)

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

/*
 * CRASH_SYSTEM halts the machine, so the mock never returns to its caller;
 * run_cleanup() below catches it and reports whether the call crashed.
 */
static int crash_calls;
static const status_$t *crash_arg;
static jmp_buf crash_jmp;
void CRASH_SYSTEM(const status_$t *st)
{
    crash_calls++;
    crash_arg = st;
    longjmp(crash_jmp, 1);
}

static int excl_stop_calls;
static const ml_$exclusion_t *excl_stop_arg;
void ML_$EXCLUSION_STOP(ml_$exclusion_t *m)
{
    excl_stop_calls++;
    excl_stop_arg = m;
}

static int old_cleanup_calls;
void DIR_$OLD_CLEANUP(void) { old_cleanup_calls++; }

static int            vp_calls;
static void          *vp_handle;
static char           vp_flag;
uint32_t dir_$validate_pages(void *handle, char crash_flag, status_$t *status_ret)
{
    vp_calls++;
    vp_handle = handle;
    vp_flag = crash_flag;
    *status_ret = status_$ok;
    return 0;
}

static int   rw_calls;
static void *rw_handle;
void dir_$release_wire(void *handle) { rw_calls++; rw_handle = handle; }

static int    rh_calls;
static void  *rh_handle_seen;
void dir_$release_handle(void *handle_ptr)
{
    rh_calls++;
    rh_handle_seen = *(void **)handle_ptr;
    *(void **)handle_ptr = NULL;
}

/*
 * dir_$map_page mock.  The test lays out a directory of DIR_PAGE_SIZE pages
 * in `pages[]`; page i's header is written by make_page().
 */
#define TEST_MAX_PAGES 4
static uint8_t  pages[TEST_MAX_PAGES][DIR_PAGE_SIZE];
static int      mp_calls;
static int16_t  mp_last_page;

void *dir_$map_page(void *handle, int16_t page_idx)
{
    (void)handle;
    mp_calls++;
    mp_last_page = page_idx;
    if (page_idx < 0 || page_idx >= TEST_MAX_PAGES) {
        return pages[0];
    }
    return pages[page_idx];
}

/* ------------------------------------------------------------------ */

/* Point the whole DIR module block at our own buffer (source-yv13). */
#undef DIR_$BLOCK_BASE
#define DIR_$BLOCK_BASE ((void *)A5_AREA)

#include "../cleanup.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

static const uid_t DIR_UID = { 0x11111111u, 0x22222222u };

/*
 * dir_$handle_t.buf holds a 32-bit target virtual address, so the test points
 * ARCH_HOST_VA_BASE at its own arena (see arch/host/arch.h) and stores an
 * offset into it.  Any non-zero VA will do; zero is nil on the target.
 */
static uint8_t wired_buf[DIR_PAGE_SIZE];
#define WIRED_BUF_VA 0x400u

static void reset(void)
{
    memset(block_store, 0, sizeof(block_store));
    memset(pages, 0, sizeof(pages));
    memset(wired_buf, 0, sizeof(wired_buf));
    ARCH_HOST_VA_BASE = (uintptr_t)wired_buf - WIRED_BUF_VA;
    crash_calls = 0;
    crash_arg = NULL;
    excl_stop_calls = 0;
    excl_stop_arg = NULL;
    old_cleanup_calls = 0;
    vp_calls = 0;
    vp_handle = NULL;
    vp_flag = 0;
    rw_calls = 0;
    rw_handle = NULL;
    rh_calls = 0;
    rh_handle_seen = NULL;
    mp_calls = 0;
    mp_last_page = -1;
    PROC1_$CURRENT = 7;
    UID_$NIL.high = 0;
    UID_$NIL.low = 0;
}

/* Run DIR_$CLEANUP; returns non-zero if CRASH_SYSTEM was reached. */
static int run_cleanup(void)
{
    if (setjmp(crash_jmp)) {
        return 1;
    }
    DIR_$CLEANUP();
    return 0;
}

/* Write a well-formed page header that the scan will accept. */
static void make_page(int idx, uint16_t page_no)
{
    dir_page_hdr_t *h = (dir_page_hdr_t *)pages[idx];
    h->kind = 0x00;                 /* (kind & 0xC0) >> 6 == 0 */
    h->version = 0x01;              /* version & 0x3F != 0     */
    h->dir_uid_high = DIR_UID.high;
    h->dir_uid_low  = DIR_UID.low;
    h->page_no = page_no;
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

TEST(the_block_layout_matches_the_image)
{
    /* The two tables and the four scalars the image addresses. */
    ASSERT_EQ(0x00E7F280u, DIR_A5_BASE_VA + DIR_LOCK_TAB_OFF);
    ASSERT_EQ(0x00E7F480u, DIR_A5_BASE_VA + DIR_HANDLE_TAB_OFF);
    ASSERT_EQ(0x00E7FC30u, DIR_A5_BASE_VA + DIR_LOCK_FREE_OFF);
    ASSERT_EQ(0x00E7FC34u, DIR_A5_BASE_VA + DIR_LOCK_IN_USE_OFF);
    ASSERT_EQ(0x00E7FC38u, DIR_A5_BASE_VA + DIR_HANDLE_FREE_OFF);
    ASSERT_EQ(0x00E7FC3Cu, DIR_A5_BASE_VA + DIR_HANDLE_IN_USE_OFF);
    ASSERT_EQ(0x00E7FC40u, DIR_A5_BASE_VA + DIR_LINK_BUF_OWNER_OFF);

    ASSERT_EQ(0x10u, sizeof(dir_$lock_entry_t));
    ASSERT_EQ(0x3Cu, sizeof(dir_$handle_t));

    /* The handle table ends exactly on DIR_$NAME_OFFSET_TABLE (0x00E7FC00). */
    ASSERT_EQ(0x00E7FC00u,
              DIR_A5_BASE_VA + DIR_HANDLE_TAB_OFF + DIR_SLOT_COUNT * 0x3C);
    /* ...and the lock table ends where the handle table starts. */
    ASSERT_EQ((unsigned long)DIR_HANDLE_TAB_OFF,
              (unsigned long)(DIR_LOCK_TAB_OFF + DIR_SLOT_COUNT * 0x10));

    /* Field offsets the routine reaches as (0x18xx,A3). */
    ASSERT_EQ(0x1888u, DIR_HANDLE_TAB_OFF + __builtin_offsetof(dir_$handle_t, owner));
    ASSERT_EQ(0x188Eu, DIR_HANDLE_TAB_OFF + __builtin_offsetof(dir_$handle_t, split_busy));
    ASSERT_EQ(0x1890u, DIR_HANDLE_TAB_OFF + __builtin_offsetof(dir_$handle_t, length));
    ASSERT_EQ(0x1898u, DIR_HANDLE_TAB_OFF + __builtin_offsetof(dir_$handle_t, buf));
    ASSERT_EQ(0x189Cu, DIR_HANDLE_TAB_OFF + __builtin_offsetof(dir_$handle_t, max_slots));
    ASSERT_EQ(0x18B0u, DIR_HANDLE_TAB_OFF + __builtin_offsetof(dir_$handle_t, next));
    ASSERT_EQ(0x18B8u, DIR_HANDLE_TAB_OFF + __builtin_offsetof(dir_$handle_t, slot_index));
    ASSERT_EQ(0x168Eu, DIR_LOCK_TAB_OFF + __builtin_offsetof(dir_$lock_entry_t, index));
}

TEST(no_slots_in_use_runs_only_the_mutex_check_and_old_cleanup)
{
    reset();
    DIR_$HANDLE_IN_USE = 0;

    ASSERT_EQ(0, run_cleanup());

    ASSERT_EQ(0, rh_calls);
    ASSERT_EQ(0, vp_calls);
    ASSERT_EQ(0, rw_calls);
    ASSERT_EQ(0, crash_calls);
    /* Owner word is zero, PROC1_$CURRENT is 7, so no handover. */
    ASSERT_EQ(0, excl_stop_calls);
    ASSERT_EQ(1, old_cleanup_calls);
}

TEST(a_slot_owned_by_another_process_is_skipped)
{
    reset();
    DIR_$HANDLE_IN_USE = (1u << 3);
    DIR_$HANDLE_TAB[3].owner = 9;           /* not PROC1_$CURRENT (7) */
    DIR_$HANDLE_TAB[3].split_busy = (int8_t)0xFF;
    DIR_$HANDLE_TAB[3].max_slots = 0;       /* would crash if visited */

    ASSERT_EQ(0, run_cleanup());

    ASSERT_EQ(0, rh_calls);
    ASSERT_EQ(0, vp_calls);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(1, old_cleanup_calls);
}

TEST(an_owned_slot_with_split_busy_clear_takes_the_short_path)
{
    reset();
    DIR_$HANDLE_IN_USE = (1u << 5);
    DIR_$HANDLE_TAB[5].owner = 7;
    DIR_$HANDLE_TAB[5].split_busy = 0;      /* >= 0: bpl at 0x00E535CE */
    DIR_$HANDLE_TAB[5].max_slots = 2;       /* the value 0x00E536CC wants */

    ASSERT_EQ(0, run_cleanup());

    /* No page scan, no validate, no unwire - just the handle release. */
    ASSERT_EQ(0, mp_calls);
    ASSERT_EQ(0, vp_calls);
    ASSERT_EQ(0, rw_calls);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(1, rh_calls);
    ASSERT_TRUE(rh_handle_seen == (void *)&DIR_$HANDLE_TAB[5]);
    ASSERT_EQ(1, old_cleanup_calls);
}

TEST(split_busy_clear_with_a_page_still_wired_crashes)
{
    reset();
    DIR_$HANDLE_IN_USE = (1u << 5);
    DIR_$HANDLE_TAB[5].owner = 7;
    DIR_$HANDLE_TAB[5].split_busy = 0;
    DIR_$HANDLE_TAB[5].max_slots = 1;       /* != 2 -> 0x00E536D4 */

    ASSERT_EQ(1, run_cleanup());

    ASSERT_EQ(1, crash_calls);
    /* The pushed argument is the longword at A5-0x4 (0x00E7DBFC). */
    ASSERT_TRUE(crash_arg == &Naming_bad_request_header_ver_err);
    ASSERT_TRUE(crash_arg == DIR_$CRASH_STATUS);
}

TEST(split_busy_with_max_slots_2_skips_straight_to_validate_pages)
{
    reset();
    DIR_$HANDLE_IN_USE = (1u << 0);
    DIR_$HANDLE_TAB[0].owner = 7;
    DIR_$HANDLE_TAB[0].split_busy = (int8_t)0xFF;
    DIR_$HANDLE_TAB[0].max_slots = 2;       /* beq at 0x00E535D8 */

    ASSERT_EQ(0, run_cleanup());

    ASSERT_EQ(0, mp_calls);
    ASSERT_EQ(0, rw_calls);
    ASSERT_EQ(1, vp_calls);
    ASSERT_TRUE(vp_handle == (void *)&DIR_$HANDLE_TAB[0]);
    /* `st -(SP)` at 0x00E536BE - the crash_flag is Domain true. */
    ASSERT_EQ(0xFF, (unsigned char)vp_flag);
    ASSERT_EQ(1, rh_calls);
}

TEST(the_backwards_scan_restores_the_wired_page_and_unwires_it)
{
    dir_$handle_t *h;
    unsigned i;

    reset();
    DIR_$HANDLE_IN_USE = (1u << 2);
    h = &DIR_$HANDLE_TAB[2];
    h->owner = 7;
    h->split_busy = (int8_t)0xFF;
    h->max_slots = 1;                       /* a page is still wired */
    h->length = 3 * DIR_PAGE_SIZE;          /* last page index = 2 */
    h->buf = WIRED_BUF_VA;

    /*
     * Every page's header must be well formed and must NOT carry its own
     * index as its page number (0x00E53654 crashes on equality).  The wired
     * buffer claims page number 0x55, which page 1 carries.
     */
    make_page(2, 0x99);
    make_page(1, 0x55);
    make_page(0, 0x77);
    for (i = 0; i < DIR_PAGE_SIZE; i++) {
        pages[1][i] = (uint8_t)(i + 0x20);
    }
    make_page(1, 0x55);                     /* rewrite the header over the fill */

    ((dir_page_hdr_t *)wired_buf)->page_no = 0x55;

    ASSERT_EQ(0, run_cleanup());

    ASSERT_EQ(0, crash_calls);
    /* Pages 2 then 1 were mapped. */
    ASSERT_EQ(2, mp_calls);
    ASSERT_EQ(1, mp_last_page);

    /* 0x400 bytes copied from the mapped page into the handle's buffer. */
    ASSERT_EQ(pages[1][0x100], wired_buf[0x100]);
    ASSERT_EQ(pages[1][DIR_PAGE_SIZE - 1], wired_buf[DIR_PAGE_SIZE - 1]);

    /* ...then the copy is invalidated: uid high cleared, flag bit 4 cleared. */
    ASSERT_EQ(0u, ((dir_page_hdr_t *)wired_buf)->dir_uid_high);
    ASSERT_EQ(0u, wired_buf[0] & 0x10);

    ASSERT_EQ(1, rw_calls);
    ASSERT_TRUE(rw_handle == (void *)h);
    ASSERT_EQ(1, vp_calls);
    ASSERT_EQ(1, rh_calls);
}

TEST(a_page_whose_number_equals_its_index_crashes)
{
    dir_$handle_t *h;

    reset();
    DIR_$HANDLE_IN_USE = (1u << 2);
    h = &DIR_$HANDLE_TAB[2];
    h->owner = 7;
    h->split_busy = (int8_t)0xFF;
    h->max_slots = 1;
    h->length = 2 * DIR_PAGE_SIZE;          /* last page index = 1 */
    h->buf = WIRED_BUF_VA;

    make_page(1, 1);                        /* page_no == index -> 0x00E5365A */
    ((dir_page_hdr_t *)wired_buf)->page_no = 1;

    ASSERT_EQ(1, run_cleanup());

    ASSERT_EQ(1, mp_calls);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0, rw_calls);
}

TEST(running_off_the_front_of_the_directory_crashes)
{
    dir_$handle_t *h;

    reset();
    DIR_$HANDLE_IN_USE = (1u << 1);
    h = &DIR_$HANDLE_TAB[1];
    h->owner = 7;
    h->split_busy = (int8_t)0xFF;
    h->max_slots = 1;
    h->length = 2 * DIR_PAGE_SIZE;          /* pages 1 then 0 */
    h->buf = WIRED_BUF_VA;

    make_page(1, 0x33);
    make_page(0, 0x44);
    /* No page carries the buffer's number, so the scan reaches index 0. */
    ((dir_page_hdr_t *)wired_buf)->page_no = 0xBEEF;

    ASSERT_EQ(1, run_cleanup());

    ASSERT_EQ(2, mp_calls);
    ASSERT_EQ(1, crash_calls);              /* 0x00E53678 */
}

TEST(a_bad_page_header_crashes)
{
    dir_$handle_t *h;

    reset();
    DIR_$HANDLE_IN_USE = (1u << 1);
    h = &DIR_$HANDLE_TAB[1];
    h->owner = 7;
    h->split_busy = (int8_t)0xFF;
    h->max_slots = 1;
    h->length = 1 * DIR_PAGE_SIZE;          /* only page 0 */
    h->buf = WIRED_BUF_VA;

    make_page(0, 0x33);
    ((dir_page_hdr_t *)pages[0])->version = 0x40;   /* & 0x3F == 0 */
    ((dir_page_hdr_t *)wired_buf)->page_no = 0x33;

    ASSERT_EQ(1, run_cleanup());

    ASSERT_EQ(1, crash_calls);
}

TEST(the_first_page_visited_fixes_the_directory_uid)
{
    dir_$handle_t *h;

    reset();
    DIR_$HANDLE_IN_USE = (1u << 1);
    h = &DIR_$HANDLE_TAB[1];
    h->owner = 7;
    h->split_busy = (int8_t)0xFF;
    h->max_slots = 1;
    h->length = 2 * DIR_PAGE_SIZE;
    h->buf = WIRED_BUF_VA;

    make_page(1, 0x33);
    make_page(0, 0x44);
    /* Page 0 belongs to a different directory -> 0x00E5364C mismatch. */
    ((dir_page_hdr_t *)pages[0])->dir_uid_low = 0xDEADBEEFu;
    ((dir_page_hdr_t *)wired_buf)->page_no = 0x44;

    ASSERT_EQ(1, run_cleanup());

    ASSERT_EQ(2, mp_calls);
    ASSERT_EQ(1, crash_calls);
}

TEST(the_link_buffer_mutex_is_released_only_by_its_owner)
{
    reset();
    DIR_$LINK_BUF_OWNER = 7;                /* == PROC1_$CURRENT */
    ASSERT_EQ(0, run_cleanup());
    ASSERT_EQ(1, excl_stop_calls);
    ASSERT_TRUE(excl_stop_arg == &DIR_$LINK_BUF_MUTEX);
    ASSERT_EQ(0, DIR_$LINK_BUF_OWNER);
    ASSERT_EQ(1, old_cleanup_calls);

    reset();
    DIR_$LINK_BUF_OWNER = 8;                /* someone else */
    ASSERT_EQ(0, run_cleanup());
    ASSERT_EQ(0, excl_stop_calls);
    ASSERT_EQ(8, DIR_$LINK_BUF_OWNER);
    ASSERT_EQ(1, old_cleanup_calls);
}

TEST(every_owned_slot_in_the_bitmap_is_visited)
{
    unsigned s;

    reset();
    for (s = 0; s < DIR_SLOT_COUNT; s++) {
        DIR_$HANDLE_IN_USE |= (1u << s);
        DIR_$HANDLE_TAB[s].owner = 7;
        DIR_$HANDLE_TAB[s].split_busy = 0;
        DIR_$HANDLE_TAB[s].max_slots = 2;
    }

    ASSERT_EQ(0, run_cleanup());

    ASSERT_EQ(DIR_SLOT_COUNT, rh_calls);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(1, old_cleanup_calls);
}

int main(void)
{
    printf("DIR_$CLEANUP (0x00E53578) tests\n");

    RUN_TEST(the_block_layout_matches_the_image);
    RUN_TEST(no_slots_in_use_runs_only_the_mutex_check_and_old_cleanup);
    RUN_TEST(a_slot_owned_by_another_process_is_skipped);
    RUN_TEST(an_owned_slot_with_split_busy_clear_takes_the_short_path);
    RUN_TEST(split_busy_clear_with_a_page_still_wired_crashes);
    RUN_TEST(split_busy_with_max_slots_2_skips_straight_to_validate_pages);
    RUN_TEST(the_backwards_scan_restores_the_wired_page_and_unwires_it);
    RUN_TEST(a_page_whose_number_equals_its_index_crashes);
    RUN_TEST(running_off_the_front_of_the_directory_crashes);
    RUN_TEST(a_bad_page_header_crashes);
    RUN_TEST(the_first_page_visited_fixes_the_directory_uid);
    RUN_TEST(the_link_buffer_mutex_is_released_only_by_its_owner);
    RUN_TEST(every_owned_slot_in_the_bitmap_is_visited);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
