/*
 * ast/test/test_touch_area.c - Unit tests for AST_$TOUCH_AREA (0x00E03548)
 *
 * The real ast/touch_area.c is #included below and the real AST_$TOUCH_AREA
 * is called; every callee it reaches is mocked here.
 *
 * Two of the function's paths are reachable without disk or network state and
 * are what these tests exercise:
 *
 *   - 0x00E035BA "the page is already installed": re-reference it, hand the
 *     PPNs back through MMAP_$RECLAIM and return early (0x00E0360E branches
 *     straight to the epilogue, so no EC_$ADVANCE and no page-fault count).
 *   - 0x00E0361C "no backing store": allocate one frame, zero it, install it
 *     and advance AST_$PMAP_IN_TRANS_EC.
 *
 * Both run-length loops (0x00E035E2 and 0x00E0374E) compare their counter
 * against zero with `tst.w / bgt`, so exactly one page is ever processed.
 * test_installed_run_stops_after_one_page pins that behaviour.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Avoid the macOS uid_t conflict - must come AFTER the system includes. */
#define uid_t ast_uid_t

/* ------------------------------------------------------------------ */
/* Test framework                                                      */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_failed = 0;
static int test_failed_flag = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                 \
    printf("  %-46s", #name);                                               \
    test_failed_flag = 0;                                                   \
    tests_run++;                                                            \
    test_##name();                                                          \
    if (test_failed_flag) { tests_failed++; } else { printf("ok\n"); }      \
} while (0)

#define CHECK_EQ(expected, actual) do {                                     \
    unsigned long long _e = (unsigned long long)(expected);                 \
    unsigned long long _a = (unsigned long long)(actual);                   \
    if (_e != _a) {                                                         \
        printf("FAILED\n    line %d: expected 0x%llx, got 0x%llx\n",        \
               __LINE__, _e, _a);                                           \
        test_failed_flag = 1;                                               \
        return;                                                             \
    }                                                                       \
} while (0)

/* ------------------------------------------------------------------ */
/* Real headers, mocked callees                                        */
/* ------------------------------------------------------------------ */

#include "ast/ast_internal.h"

/* Backing storage for the AST/MMAP/MMU tables the function walks. */
#define TEST_N_SEGS   4
#define TEST_N_PAGES  32
#define TEST_N_FRAMES 64

static segmap_entry_t test_segmap[TEST_N_SEGS * TEST_N_PAGES];
static aste_t         test_astes[TEST_N_SEGS];
static mmape_t        test_mmapes[TEST_N_FRAMES];
static uint32_t       test_pft[TEST_N_FRAMES];

segmap_entry_t *ast_segmap_base = test_segmap;
aste_t         *ast_aste_base   = test_astes;
mmape_t        *mmap_mmape_base = test_mmapes;
uint32_t       *mmu_pft_base    = test_pft;

uint32_t ast_ws_flt_cnt;
uint32_t ast_page_flt_cnt;
ec_$eventcount_t ast_pmap_in_trans_ec;

/* Globals the body reads outside the two tested paths. */
uid_t     ANON_$UID = { 0x11112222u, 0x33334444u };
/* The AREA_ module data block is one object (source-vm49); AREA_$PARTNER and
 * AREA_$PARTNER_PKT_SIZE are #define aliases onto its fields, so defining
 * AREA_$GLOBALS supplies both. */
area_$globals_t AREA_$GLOBALS;
int8_t    NETLOG_$OK_TO_LOG;
uint16_t  PROC1_$CURRENT;
uint32_t  PROC_STATS_BASE[PROC1_MAX_PROCESSES * 4];

/* Mock call records. */
static int      mock_wait_calls;
static int      mock_lock_depth;
static int      mock_reclaim_calls;
static uint32_t *mock_reclaim_array;
static uint16_t mock_reclaim_count;
static int8_t   mock_reclaim_wired;
static int      mock_install_calls;
static uint32_t *mock_install_array;
static uint16_t mock_install_count;
static int      mock_alloc_calls;
static uint32_t mock_alloc_arg;
static uint32_t mock_alloc_ppn;
static int16_t  mock_alloc_result;
static int      mock_zero_page_calls;
static uint32_t mock_zero_page_ppn;
static int      mock_clear_trans_calls;
static int      mock_ec_advance_calls;
static int      mock_crash_calls;
static int      mock_netlog_calls;
static int      mock_free_calls;

/* The already-installed loop spins here until the caller clears bit 31. */
static uint32_t *mock_wait_target;
static int       mock_wait_clear_after;

static void reset_mocks(void)
{
    memset(test_segmap, 0, sizeof(test_segmap));
    memset(test_astes, 0, sizeof(test_astes));
    memset(test_mmapes, 0, sizeof(test_mmapes));
    memset(test_pft, 0, sizeof(test_pft));
    memset(PROC_STATS_BASE, 0, sizeof(PROC_STATS_BASE));
    ast_ws_flt_cnt = 0;
    ast_page_flt_cnt = 0;
    memset(&ast_pmap_in_trans_ec, 0, sizeof(ast_pmap_in_trans_ec));
    NETLOG_$OK_TO_LOG = 0;
    PROC1_$CURRENT = 0;
    mock_wait_calls = 0;
    mock_lock_depth = 0;
    mock_reclaim_calls = 0;
    mock_reclaim_array = NULL;
    mock_reclaim_count = 0xFFFF;
    mock_reclaim_wired = -1;
    mock_install_calls = 0;
    mock_install_array = NULL;
    mock_install_count = 0xFFFF;
    mock_alloc_calls = 0;
    mock_alloc_arg = 0;
    mock_alloc_ppn = 7;
    mock_alloc_result = 1;
    mock_zero_page_calls = 0;
    mock_zero_page_ppn = 0;
    mock_clear_trans_calls = 0;
    mock_ec_advance_calls = 0;
    mock_crash_calls = 0;
    mock_netlog_calls = 0;
    mock_free_calls = 0;
    mock_wait_target = NULL;
    mock_wait_clear_after = 0;
}

void ML_$LOCK(int16_t lock_id)   { (void)lock_id; mock_lock_depth++; }
void ML_$UNLOCK(int16_t lock_id) { (void)lock_id; mock_lock_depth--; }

void ast_$wait_for_page_transition(void)
{
    mock_wait_calls++;
    if (mock_wait_target != NULL && mock_wait_calls >= mock_wait_clear_after) {
        *mock_wait_target &= ~0x80000000u;
    }
}

int16_t ast_$allocate_pages(int16_t count, int16_t min_count,
                            uint32_t *ppn_array)
{
    mock_alloc_calls++;
    /*
     * The two word arguments as the original pushes them: count at
     * (0x8,A6) and the minimum at (0xa,A6), i.e. the high and low halves
     * of the longword the tests used to check.
     */
    mock_alloc_arg = ((uint32_t)(uint16_t)count << 16) | (uint16_t)min_count;
    ppn_array[0] = mock_alloc_ppn;
    return mock_alloc_result;
}

void ast_$clear_transition_bits(uint32_t *segmap, uint16_t count)
{
    (void)segmap; (void)count;
    mock_clear_trans_calls++;
}

void ZERO_PAGE(uint32_t ppn)
{
    mock_zero_page_calls++;
    mock_zero_page_ppn = ppn;
}

void MMAP_$RECLAIM(uint32_t *vpn_array, uint16_t count, int8_t use_wired)
{
    mock_reclaim_calls++;
    mock_reclaim_array = vpn_array;
    mock_reclaim_count = count;
    mock_reclaim_wired = use_wired;
}

void MMAP_$INSTALL_LIST(uint32_t *vpn_array, uint16_t count, int8_t use_wired)
{
    (void)use_wired;
    mock_install_calls++;
    mock_install_array = vpn_array;
    mock_install_count = count;
}

void MMAP_$FREE(uint32_t vpn) { (void)vpn; mock_free_calls++; }

void NETBUF_$RTN_DAT(uint32_t addr) { (void)addr; }
void NETBUF_$GET_DAT(uint32_t *addr_out) { *addr_out = 0; }

int16_t NETWORK_$READ_AHEAD(void *net_info, void *uid, uint32_t *ppn_array,
                            uint16_t page_size, int16_t count,
                            int8_t no_read_ahead, uint8_t flags,
                            clock_t *dtm, clock_t *clock,
                            clock_t *acl_info, status_$t *status)
{
    (void)net_info; (void)uid; (void)ppn_array; (void)page_size;
    (void)count; (void)no_read_ahead; (void)flags;
    (void)dtm; (void)clock; (void)acl_info;
    *status = status_$ok;
    return 0;
}

void DISK_$GET_QBLKS(int16_t count, int32_t *qblk_head, uint32_t *qblk_tail)
{
    (void)count;
    *qblk_head = 0;
    *qblk_tail = 0;
}

void DISK_$RTN_QBLKS(int16_t count, int32_t qblk_head, uint32_t qblk_tail)
{
    (void)count; (void)qblk_head; (void)qblk_tail;
}

void DISK_$READ_MULTI(uint16_t vol_idx, int16_t flags1, int16_t flags2,
                      int32_t qblk_head, uint32_t qblk_tail,
                      int16_t *pages_read, status_$t *status)
{
    (void)vol_idx; (void)flags1; (void)flags2;
    (void)qblk_head; (void)qblk_tail;
    *pages_read = 0;
    *status = status_$ok;
}

void CRASH_SYSTEM(const status_$t *status_p) { (void)status_p; mock_crash_calls++; }

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid,
                    uint16_t param3, uint16_t param4,
                    uint16_t param5, uint16_t param6,
                    uint16_t param7, uint16_t param8)
{
    (void)kind; (void)uid; (void)param3; (void)param4;
    (void)param5; (void)param6; (void)param7; (void)param8;
    mock_netlog_calls++;
}

void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; mock_ec_advance_calls++; }

/* The implementation under test. */
#include "../touch_area.c"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/*
 * The area flavour of the segment map is 1-based in seg_index: 0x00E0359C
 * builds the pointer as SEGMAP_BASE + seg_index*0x80 - 0x80 + page*4.
 */
#define TEST_SEG   1
#define TEST_AREA  3

static uint32_t *seg_entry(int page)
{
    return (uint32_t *)((char *)ast_segmap_base + TEST_SEG * 0x80 - 0x80 +
                        page * 4);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * 0x00E035B4 "btst.l #0xe,D4" selects the installed-page path, which ends at
 * 0x00E0360E with a branch straight to the epilogue.
 */
TEST(installed_page_reclaims_and_returns_early)
{
    uint32_t ppn_array[TEST_N_PAGES];
    status_$t st = -1;

    reset_mocks();
    memset(ppn_array, 0xEE, sizeof(ppn_array));

    /* SEGMAP_VALID plus PPN 5 in the low word. */
    *seg_entry(0) = 0x40000000u | 5u;

    AST_$TOUCH_AREA(TEST_AREA, TEST_SEG, 0, 0x20, ppn_array, &st);

    CHECK_EQ(status_$ok, st);
    /* 0x00E035DC: the PPN is zero-extended from the segment map's low word. */
    CHECK_EQ(5, ppn_array[0]);
    /* 0x00E035C0 "bset.b #5" sets 0x20 in the entry's most significant byte. */
    CHECK_EQ(0x60000005u, *seg_entry(0));
    /* 0x00E035CA: PFT_BASE[ppn] low word gains 0x2000. */
    CHECK_EQ(0x2000u, test_pft[5] & 0xFFFFu);
    /* 0x00E03600 MMAP_$RECLAIM(ppn_array, 1, 0) */
    CHECK_EQ(1, mock_reclaim_calls);
    CHECK_EQ(1, mock_reclaim_count);
    CHECK_EQ(0, mock_reclaim_wired);
    CHECK_EQ((uintptr_t)ppn_array, (uintptr_t)mock_reclaim_array);
    /* 0x00E0360A AST_$WS_FLT_CNT += count */
    CHECK_EQ(1, ast_ws_flt_cnt);
    /* The early return skips the common tail entirely. */
    CHECK_EQ(0, ast_page_flt_cnt);
    CHECK_EQ(0, mock_ec_advance_calls);
    CHECK_EQ(0, mock_install_calls);
}

/*
 * 0x00E035E2 "tst.w D2w / bgt" - the run-length limit is a comparison against
 * zero, so the loop always exits after the first page even when the next
 * entry is a perfectly good installed page.
 */
TEST(installed_run_stops_after_one_page)
{
    uint32_t ppn_array[TEST_N_PAGES];
    status_$t st = -1;

    reset_mocks();
    memset(ppn_array, 0xEE, sizeof(ppn_array));

    *seg_entry(0) = 0x40000000u | 5u;
    *seg_entry(1) = 0x40000000u | 6u;

    AST_$TOUCH_AREA(TEST_AREA, TEST_SEG, 0, 0x20, ppn_array, &st);

    CHECK_EQ(1, mock_reclaim_count);
    /* The second entry is left completely untouched. */
    CHECK_EQ(0x40000006u, *seg_entry(1));
    CHECK_EQ(0xEEEEEEEEu, ppn_array[1]);
}

/*
 * 0x00E035AE "tst.w (A2) / bmi" - the entry point spins on
 * ast_$wait_for_page_transition while the first page is in transition.
 */
TEST(waits_while_first_page_is_in_transition)
{
    uint32_t ppn_array[TEST_N_PAGES];
    status_$t st = -1;

    reset_mocks();
    *seg_entry(0) = 0x80000000u | 0x40000000u | 5u;
    mock_wait_target = seg_entry(0);
    mock_wait_clear_after = 3;

    AST_$TOUCH_AREA(TEST_AREA, TEST_SEG, 0, 0x20, ppn_array, &st);

    CHECK_EQ(3, mock_wait_calls);
    CHECK_EQ(1, mock_reclaim_calls);
}

/*
 * 0x00E0361C: neither installed nor backed by disk - allocate one frame,
 * zero it, install it and fall through the common tail.
 */
TEST(unbacked_page_is_zero_filled_and_installed)
{
    uint32_t ppn_array[TEST_N_PAGES];
    status_$t st = -1;

    reset_mocks();
    mock_alloc_ppn = 7;
    *seg_entry(0) = 0;              /* not valid, no disk address */

    AST_$TOUCH_AREA(TEST_AREA, TEST_SEG, 0, 0x20, ppn_array, &st);

    CHECK_EQ(status_$ok, st);
    /* 0x00E03624: the allocate request is the longword 0x00010001. */
    CHECK_EQ(0x10001u, mock_alloc_arg);
    CHECK_EQ(1, mock_zero_page_calls);
    CHECK_EQ(7, mock_zero_page_ppn);

    /* 0x00E03942-0x00E03952: the frame's MMAPE is reset and re-flagged. */
    CHECK_EQ(0, test_mmapes[7].wire_count);
    CHECK_EQ(MMAPE_FLAG1_IMPURE, test_mmapes[7].flags1);
    CHECK_EQ(MMAPE_FLAG2_ON_DISK, test_mmapes[7].flags2);
    CHECK_EQ(0, test_mmapes[7].seg_offset);      /* page + i - 1 == 0 */
    CHECK_EQ(TEST_SEG, test_mmapes[7].segment);

    /* 0x00E03974-0x00E0399C: PPN planted, referenced+valid set, in-trans off */
    CHECK_EQ(0x60000007u, *seg_entry(0));
    CHECK_EQ(0x2000u, test_pft[7] & 0xFFFFu);

    /* 0x00E039B2 MMAP_$INSTALL_LIST(ppn_array, 1, 0) */
    CHECK_EQ(1, mock_install_calls);
    CHECK_EQ(1, mock_install_count);
    /* 0x00E039CE: ASTE_BASE[seg_index - 1].page_count += pages_done */
    CHECK_EQ(1, test_astes[TEST_SEG - 1].page_count);
    /* 0x00E039D2 / 0x00E03A1C */
    CHECK_EQ(1, ast_page_flt_cnt);
    CHECK_EQ(1, mock_ec_advance_calls);
    /* Nothing was in transition longer than requested. */
    CHECK_EQ(0, mock_clear_trans_calls);
    CHECK_EQ(0, mock_crash_calls);
    /* NETLOG_$OK_TO_LOG >= 0 so 0x00E039D6 skips the log call. */
    CHECK_EQ(0, mock_netlog_calls);
}

/*
 * 0x00E03930 "tst.b (-0x1ffb,A3) / bpl" - a frame that is still in a working
 * set list when it comes back from the allocator is a fatal inconsistency.
 */
TEST(installing_a_frame_still_in_a_wsl_crashes)
{
    uint32_t ppn_array[TEST_N_PAGES];
    status_$t st = -1;

    reset_mocks();
    mock_alloc_ppn = 9;
    test_mmapes[9].flags1 = MMAPE_FLAG1_IN_WSL;
    *seg_entry(0) = 0;

    AST_$TOUCH_AREA(TEST_AREA, TEST_SEG, 0, 0x20, ppn_array, &st);

    CHECK_EQ(1, mock_crash_calls);
}

int main(void)
{
    printf("AST_$TOUCH_AREA (0x00E03548) tests\n");
    RUN_TEST(installed_page_reclaims_and_returns_early);
    RUN_TEST(installed_run_stops_after_one_page);
    RUN_TEST(waits_while_first_page_is_in_transition);
    RUN_TEST(unbacked_page_is_zero_filled_and_installed);
    RUN_TEST(installing_a_frame_still_in_a_wsl_crashes);
    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
