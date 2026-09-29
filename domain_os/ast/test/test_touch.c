/*
 * ast/test/test_touch.c - Unit tests for AST_$TOUCH (0x00E030C0)
 *
 * The test #includes ast/touch.c directly and drives the real routine
 * through mocks.  Pins:
 *
 *   - the access checks (OS-only vs type-8 process; concurrency token
 *     vs attribute bit 11) and their statuses;
 *   - the installed path: consecutive installed unwired entries wired
 *     and returned, RECLAIM for real frames, WS_FLT_CNT;
 *   - the bit-22 path through ast_$count_valid_pages (kind 8);
 *   - the EOF / grow-ahead clipping;
 *   - the empty-local path (setup_page_read then count_valid_pages);
 *   - the read path (local disk / remote network), the per-process
 *     statistics, the install of the read pages (MMAPE fields, bits 29
 *     and 30, bit 31 off, PFT), INSTALL_LIST, page count, the log;
 *   - unfilled entries released; no page -> bit 31.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/touch.c"

#define TEST_N_PAGES 32
#define TEST_N_FRAMES 0x1000
static segmap_entry_t test_segmap[3 * TEST_N_PAGES];
static mmape_t        test_mmapes[TEST_N_FRAMES];
static uint32_t       test_pft[TEST_N_FRAMES];
segmap_entry_t *ast_segmap_base = &test_segmap[TEST_N_PAGES];
mmape_t        *mmap_mmape_base = test_mmapes;
uint32_t       *mmu_pft_base    = test_pft;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
uint16_t  PROC1_$CURRENT;
uint32_t  ast_ws_flt_cnt, ast_page_flt_cnt;
uint16_t  ast_grow_ahead_cnt;
ec_$eventcount_t ast_pmap_in_trans_ec;
int8_t    NETLOG_$OK_TO_LOG;

static aote_t test_aote;
static aste_t test_aste;

void ast_$wait_for_page_transition(void) { }
static int advance_calls;
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advance_calls++; }
static int reclaim_calls; static uint16_t reclaim_count; static boolean reclaim_wired;
void MMAP_$RECLAIM(uint32_t *a, uint16_t n, boolean w) { (void)a; reclaim_calls++; reclaim_count = n; reclaim_wired = w; }
static int install_calls; static uint16_t install_count; static int8_t install_wired;
void MMAP_$INSTALL_LIST(uint32_t *a, uint16_t n, int8_t w) { (void)a; install_calls++; install_count = n; install_wired = w; }
static int cvp_calls; static uint32_t *cvp_entry; static int16_t cvp_count, cvp_result; static uint16_t cvp_flags;
int16_t ast_$count_valid_pages(uint32_t *e, int16_t n, uint16_t f, uint32_t *arr, status_$t *st)
{
    int i; cvp_calls++; cvp_entry = e; cvp_count = n; cvp_flags = f;
    for (i = 0; i < cvp_result; i++) arr[i] = 0x500 + i;
    *st = status_$ok; return cvp_result;
}
static int setup_calls; static uint16_t setup_page, setup_count, setup_flags; static status_$t setup_status;
void ast_$setup_page_read(aste_t *a, uint32_t *e, uint16_t p, uint16_t n, uint16_t f, status_$t *st)
{
    (void)a; (void)e; setup_calls++; setup_page = p; setup_count = n; setup_flags = f; *st = setup_status;
}
static int rap_calls; static uint32_t *rap_entry; static uint16_t rap_page, rap_count; static int16_t rap_result; static status_$t rap_status;
int16_t ast_$read_area_pages(aste_t *a, uint32_t *e, uint32_t *arr, uint16_t p, uint16_t n, status_$t *st)
{
    int i; (void)a; rap_calls++; rap_entry = e; rap_page = p; rap_count = n;
    for (i = 0; i < rap_result; i++) arr[i] = 0x600 + i;
    *st = rap_status; return rap_result;
}
static int rapn_calls; static uint8_t rapn_flags; static int16_t rapn_result; static uint16_t rapn_count;
int16_t ast_$read_area_pages_network(aste_t *a, uint32_t *e, uint32_t *arr, uint16_t p, uint16_t n, uint8_t f, status_$t *st)
{
    int i; (void)a; (void)e; (void)p; rapn_calls++; rapn_flags = f; rapn_count = n;
    for (i = 0; i < rapn_result; i++) arr[i] = 0x700 + i;
    *st = status_$ok; return rapn_result;
}
static int ctb_calls; static uint32_t *ctb_entry; static uint16_t ctb_count;
void ast_$clear_transition_bits(uint32_t *e, uint16_t n) { ctb_calls++; ctb_entry = e; ctb_count = n; }
static int log_calls; static uint16_t log_kind, log_p3, log_p4, log_p5, log_p6, log_p7;
void NETLOG_$LOG_IT(uint16_t k, uint32_t *u, uint16_t p3, uint16_t p4, uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8)
{
    (void)u; (void)p8; log_calls++; log_kind = k; log_p3 = p3; log_p4 = p4; log_p5 = p5; log_p6 = p6; log_p7 = p7;
}
static int crash_calls;
void CRASH_SYSTEM(const status_$t *s) { (void)s; crash_calls++; }

static uint32_t *row1(void) { return (uint32_t *)&test_segmap[TEST_N_PAGES]; }

static void reset_state(void)
{
    memset(test_segmap, 0, sizeof(test_segmap));
    memset(test_mmapes, 0, sizeof(test_mmapes));
    memset(test_pft, 0, sizeof(test_pft));
    memset(PROC1_$DATA.stats, 0, sizeof(PROC1_$DATA.stats));
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));
    memset(&test_aote, 0, sizeof(test_aote));
    memset(&test_aste, 0, sizeof(test_aste));
    test_aste.aote = &test_aote; test_aste.seg_index = 1; test_aste.segment = 2;
    test_aote.length = 0x100000;
    PROC1_$CURRENT = 3; ast_ws_flt_cnt = ast_page_flt_cnt = 0; ast_grow_ahead_cnt = 4;
    NETLOG_$OK_TO_LOG = 0;
    advance_calls = reclaim_calls = install_calls = cvp_calls = setup_calls = 0;
    rap_calls = rapn_calls = ctb_calls = log_calls = crash_calls = 0; rapn_count = 0;
    cvp_result = 0; setup_status = status_$ok; rap_result = 0; rap_status = status_$ok; rapn_result = 0;
}

TEST(access_checks)
{
    uint32_t ppns[32]; status_$t status = 0;

    test_aote.access_flags = (int8_t)0x80; PROC1_$DATA.type[3] = 8;
    ASSERT_EQ(0, AST_$TOUCH(&test_aste, 0, 0, 1, ppns, &status, 0));
    ASSERT_EQ(status_$ast_only_local_access_allowed, status);
    ASSERT_EQ(0, test_aote.flags);

    reset_state();
    test_aote.blocks = 5;
    ASSERT_EQ(0, AST_$TOUCH(&test_aste, 6, 0, 1, ppns, &status, 0));
    ASSERT_EQ(status_$pmap_read_concurrency_violation, status);
    test_aote.attr_flags_hi = 0x08;             /* bit 11 allows it */
    row1()[0] = SEGMAP_VALID | 0x300;
    ASSERT_EQ(1, AST_$TOUCH(&test_aste, 6, 0, 1, ppns, &status, 0));
    ASSERT_EQ(status_$ok, status);
}

TEST(installed_run)
{
    uint32_t ppns[32]; status_$t status = 0x77; uint16_t r;
    uint32_t *m = row1();

    m[4] = SEGMAP_VALID | 0x300; m[5] = SEGMAP_VALID | 0x301;
    m[6] = SEGMAP_VALID | SEGMAP_WIRED | 0x302;  /* stops the run */
    r = AST_$TOUCH(&test_aste, 0, 4, 8, ppns, &status, 0x20);

    ASSERT_EQ(2, r); ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x300, ppns[0]); ASSERT_EQ(0x301, ppns[1]);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x300, m[4]);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x301, m[5]);
    ASSERT_EQ(1, reclaim_calls); ASSERT_EQ(2, reclaim_count); ASSERT_EQ(0xFF, (uint8_t)reclaim_wired);
    ASSERT_EQ(2, ast_ws_flt_cnt);
    ASSERT_EQ(AOTE_FLAG_BUSY | AOTE_FLAG_TOUCHED, test_aote.flags);
    ASSERT_EQ(ASTE_FLAG_LOCKED, test_aste.flags);
    ASSERT_EQ(0, install_calls); ASSERT_EQ(0, ast_page_flt_cnt);

    /* an unreal frame is not reclaimed; the run is clipped to `count` */
    reset_state();
    m[0] = SEGMAP_VALID | 0x1; m[1] = SEGMAP_VALID | 0x2; m[2] = SEGMAP_VALID | 0x3;
    r = AST_$TOUCH(&test_aste, 0, 0, 2, ppns, &status, 0);
    ASSERT_EQ(2, r); ASSERT_EQ(0, reclaim_calls); ASSERT_EQ(SEGMAP_VALID | 0x3, m[2]);
}

TEST(bit22_run_counted)
{
    uint32_t ppns[32]; status_$t status = 0; uint16_t r;
    uint32_t *m = row1();

    m[2] = 0x00400000; m[3] = 0x00400000; m[4] = 0x00000005;
    cvp_result = 2;
    NETLOG_$OK_TO_LOG = -1;
    r = AST_$TOUCH(&test_aste, 0, 2, 8, ppns, &status, 0x08);

    ASSERT_EQ(2, r);
    ASSERT_EQ(1, cvp_calls); ASSERT_EQ((uintptr_t)&m[2], (uintptr_t)cvp_entry);
    ASSERT_EQ(2, cvp_count); ASSERT_EQ(0x08, cvp_flags);
    ASSERT_EQ(0, ctb_calls);
    ASSERT_EQ(AOTE_FLAG_BUSY | AOTE_FLAG_TOUCHED, test_aote.flags);
    /* installed: pages 2 and 3 with frames 0x500/0x501; only the low word
     * is replaced, so bit 22 survives */
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x00400000u | 0x500, m[2]);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x00400000u | 0x501, m[3]);
    ASSERT_EQ(MMAPE_FLAG1_IMPURE, test_mmapes[0x500].flags1);
    ASSERT_EQ(3, test_mmapes[0x501].seg_offset);
    ASSERT_EQ(1, test_mmapes[0x500].segment);
    ASSERT_EQ(0x2000, test_pft[0x500]);
    ASSERT_EQ(1, install_calls); ASSERT_EQ(2, install_count); ASSERT_EQ(0, install_wired);
    ASSERT_EQ(2, test_aste.page_count);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ(2, ast_page_flt_cnt);
    ASSERT_EQ(1, log_calls); ASSERT_EQ(8, log_kind); ASSERT_EQ(2, log_p3); ASSERT_EQ(2, log_p4);
    ASSERT_EQ(0x500, log_p5); ASSERT_EQ(2, log_p6); ASSERT_EQ(0, log_p7);
}

TEST(eof_and_grow)
{
    uint32_t ppns[32]; status_$t status = 0; uint16_t r;
    uint32_t *m = row1();

    /* length 0x10800: last page 0x41; segment 2 page 2 = 0x42 is beyond */
    test_aote.length = 0x10800;
    r = AST_$TOUCH(&test_aste, 0, 2, 4, ppns, &status, 0);
    ASSERT_EQ(0, r); ASSERT_EQ(status_$ast_eof, status);

    /* grow with a full-segment request: clipped to GROW_AHEAD_CNT (4);
     * the empty local entries get blocks from setup_page_read */
    reset_state();
    test_aote.length = 0x10800; cvp_result = 4;
    r = AST_$TOUCH(&test_aste, 0, 2, 0x20, ppns, &status, 0x01);
    ASSERT_EQ(4, r); ASSERT_EQ(1, setup_calls); ASSERT_EQ(4, setup_count);
    ASSERT_EQ(0, rap_calls);

    /* grow with a partial request: one page */
    reset_state();
    test_aote.length = 0x10800; cvp_result = 1;
    r = AST_$TOUCH(&test_aste, 0, 2, 6, ppns, &status, 0x01);
    ASSERT_EQ(1, setup_count);

    /* flags bit 1 keeps the whole clipped run */
    reset_state();
    test_aote.length = 0x10800; cvp_result = 6;
    r = AST_$TOUCH(&test_aste, 0, 2, 6, ppns, &status, 0x03);
    ASSERT_EQ(6, setup_count);

    /* in-file clip: length 0x11000 -> last page 0x43 -> pages 2,3 only */
    reset_state();
    test_aote.length = 0x11000; rap_result = 2;
    m[2] = 0x11; m[3] = 0x12; m[4] = 0x13;
    r = AST_$TOUCH(&test_aste, 0, 2, 8, ppns, &status, 0);
    ASSERT_EQ(2, rap_count);
}

TEST(empty_local_then_disk_read)
{
    uint32_t ppns[32]; status_$t status = 0; uint16_t r;
    uint32_t *m = row1();

    /* empty entries 0..1, then one with a disk address */
    m[2] = 0x00000777;
    cvp_result = 2;
    r = AST_$TOUCH(&test_aste, 0, 0, 8, ppns, &status, 0x40);
    ASSERT_EQ(2, r);
    ASSERT_EQ(1, setup_calls); ASSERT_EQ(0, setup_page); ASSERT_EQ(2, setup_count); ASSERT_EQ(0x40, setup_flags);
    ASSERT_EQ(1, cvp_calls); ASSERT_EQ(2, cvp_count);
    ASSERT_EQ(0, rap_calls);

    /* setup failure: nothing counted, the two entries released, bit 31 */
    reset_state();
    m[2] = 0x00000777; setup_status = 0x00010002;
    r = AST_$TOUCH(&test_aste, 0, 0, 8, ppns, &status, 0);
    ASSERT_EQ(0, r); ASSERT_EQ(0, cvp_calls);
    ASSERT_EQ(1, ctb_calls); ASSERT_EQ((uintptr_t)&m[0], (uintptr_t)ctb_entry); ASSERT_EQ(2, ctb_count);
    ASSERT_EQ(0x80010002u, (uint32_t)status);

    /* disk read of a run: stops at an empty entry; stats +0x00 */
    reset_state();
    m[3] = 0x11; m[4] = 0x12; m[5] = 0; rap_result = 2;
    NETLOG_$OK_TO_LOG = -1;
    r = AST_$TOUCH(&test_aste, 0, 3, 8, ppns, &status, 0);
    ASSERT_EQ(2, r); ASSERT_EQ((uintptr_t)&m[3], (uintptr_t)rap_entry);
    ASSERT_EQ(3, rap_page); ASSERT_EQ(2, rap_count);
    ASSERT_EQ(2, PROC1_$DATA.stats[3].stat[0]); ASSERT_EQ(0, PROC1_$DATA.stats[3].stat[1]);
    ASSERT_EQ(AOTE_FLAG_BUSY | AOTE_FLAG_TOUCHED, test_aote.flags);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x600, m[3]);
    ASSERT_EQ(0x11, test_mmapes[0x600].disk_addr);
    ASSERT_EQ(2, log_kind);

    /* short read: the unfilled entry is released */
    reset_state();
    m[3] = 0x11; m[4] = 0x12; m[5] = 0x13; rap_result = 1;
    r = AST_$TOUCH(&test_aste, 0, 3, 3, ppns, &status, 0x08);
    ASSERT_EQ(1, r); ASSERT_EQ(3, rap_count);
    ASSERT_EQ(1, ctb_calls); ASSERT_EQ((uintptr_t)&m[4], (uintptr_t)ctb_entry); ASSERT_EQ(2, ctb_count);
    ASSERT_EQ(1, PROC1_$DATA.stats[3].stat[1]);
    ASSERT_EQ(MMAPE_FLAG1_IMPURE, test_mmapes[0x600].flags1);
}

TEST(remote_read)
{
    uint32_t ppns[32]; status_$t status = 0; uint16_t r;
    uint32_t *m = row1();

    test_aote.remote_flag = (int8_t)0x80;
    /* a remote run does not stop at empty entries */
    m[0] = 0; m[1] = 0x5; m[2] = 0;
    rapn_result = 3;
    NETLOG_$OK_TO_LOG = -1;
    r = AST_$TOUCH(&test_aste, 0, 0, 3, ppns, &status, 0x01);
    ASSERT_EQ(3, r); ASSERT_EQ(1, rapn_calls); ASSERT_EQ(0xFF, rapn_flags);
    /* 0x00E0334C: the remote run loops past the empty m[2] as well, so all
     * three entries are in the run handed to the network read */
    ASSERT_EQ(3, rapn_count);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x702, m[2]);
    ASSERT_EQ(0, setup_calls);
    ASSERT_EQ(AOTE_FLAG_BUSY, test_aote.flags);   /* no TOUCHED on the remote path */
    ASSERT_EQ(1, log_p7);
    ASSERT_EQ(3, ast_page_flt_cnt);
}

int main(void)
{
    printf("test_touch (AST_$TOUCH 0x00E030C0)\n");

    RUN_TEST(access_checks);
    RUN_TEST(installed_run);
    RUN_TEST(bit22_run_counted);
    RUN_TEST(eof_and_grow);
    RUN_TEST(empty_local_then_disk_read);
    RUN_TEST(remote_read);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
