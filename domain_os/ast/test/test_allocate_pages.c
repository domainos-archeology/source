/*
 * ast/test/test_allocate_pages.c - Unit tests for ast_$allocate_pages
 *                                  (0x00E00D46) and its nested NETLOG
 *                                  helper at 0x00E00CAC
 *
 * The test #includes ast/allocate_pages.c directly and drives the real
 * routine through mocked callees.  Its subject is bead source-0oq1, the
 * helper's argument list:
 *
 *   - the helper takes THREE stack arguments (pmape, seg word, ppn long)
 *     and reads two more values uplevel out of the parent frame through the
 *     static link at 0x00E00CC0 - the parent's `allocated` counter
 *     ((-0xe,A3)) and its `min_count` argument ((0xa,A3));
 *   - the ASTE cursor A4 is 0xEC5400 + seg*0x14, so (-0x10,A4) is the
 *     `aote` field and (-0x8,A4) the `segment` field of AST_ASTE_ENTRY(seg);
 *     the tree subtracted a further 0x10 / 0x08;
 *   - NETLOG_$LOG_IT gets (4, uid, timestamp, pmape->seg_offset,
 *     ppn LOW word, allocated, min_count, 0);
 *   - `bclr.b #0x6,(-0x80,A2)` clears bit 30 of the segment-map longword,
 *     which is the same bit `btst.l #0xe` tested, not the low byte's bit 6.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>

/* Avoid the macOS uid_t conflict - must come AFTER the system includes. */
#define uid_t ast_uid_t

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
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

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                            \
        printf("FAILED\n    Assertion failed at line %d: %s\n",               \
               __LINE__, #cond);                                              \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/ast_internal.h"
#include "netlog/netlog.h"
#include "anon/anon.h"
#include "misc/crash_system.h"

/* ==========================================================================
 * Storage for the tables the routine walks
 * ========================================================================== */

#define TEST_N_SEGS   4
#define TEST_N_PAGES  32
#define TEST_N_FRAMES 0x1000  /* the PFT is indexed by ppn, up to 0xFFF */

/* The AST_ module blocks (ast/ast.h) and the segment map (pmap/pmap.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
static uint32_t       test_pft[TEST_N_FRAMES];

MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
uint32_t       *mmu_pft_base    = test_pft;

/* The two counters ast/allocate_pages.c reaches on a non-m68k build. */

int8_t   NETLOG_$OK_TO_LOG;
uid_t    ANON_$UID;

MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);
MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

/* MMAP_$ALLOC_FREE / MMAP_$ALLOC_PURE: scripted per call. */
#define MAX_ALLOC 8
static int      free_calls;
static uint16_t free_result[MAX_ALLOC];
static int      pure_calls;
static uint16_t pure_result[MAX_ALLOC];
static uint32_t pure_ppns[MAX_ALLOC][8];

uint16_t MMAP_$ALLOC_FREE(uint32_t *vpn_array, uint16_t count)
{
    uint16_t n;

    (void)vpn_array;
    (void)count;
    /*
     * The image's retry loop (0x00E00E5A `bcs` back to 0x00E00D6C) has no
     * bound, so a test that never reaches min_count would spin forever.
     */
    if (free_calls >= MAX_ALLOC) {
        printf("FAILED\n    the retry loop never reached min_count\n");
        exit(1);
    }
    n = free_result[free_calls];
    free_calls++;
    return n;
}

uint16_t MMAP_$ALLOC_PURE(uint32_t *vpn_array, uint16_t count)
{
    int c = pure_calls;
    uint16_t n = (c < MAX_ALLOC) ? pure_result[c] : 0;
    uint16_t i;

    (void)count;
    for (i = 0; i < n && i < 8; i++) {
        vpn_array[i] = pure_ppns[c][i];
    }
    pure_calls++;
    return n;
}

static int   purifier_calls;
static int8_t purifier_last;

void PMAP_$WAKE_PURIFIER(int8_t wait)
{
    purifier_calls++;
    purifier_last = wait;
}

/* NETLOG_$LOG_IT */
#define MAX_LOG 8
static int      log_calls;
static uint16_t log_kind[MAX_LOG];
static uint32_t log_uid_high[MAX_LOG];
static uint32_t log_uid_low[MAX_LOG];
static uint16_t log_p3[MAX_LOG], log_p4[MAX_LOG], log_p5[MAX_LOG];
static uint16_t log_p6[MAX_LOG], log_p7[MAX_LOG], log_p8[MAX_LOG];

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid,
                    uint16_t param3, uint16_t param4,
                    uint16_t param5, uint16_t param6,
                    uint16_t param7, uint16_t param8)
{
    int i = log_calls;

    if (i < MAX_LOG) {
        log_kind[i] = kind;
        log_uid_high[i] = uid[0];
        log_uid_low[i] = uid[1];
        log_p3[i] = param3; log_p4[i] = param4; log_p5[i] = param5;
        log_p6[i] = param6; log_p7[i] = param7; log_p8[i] = param8;
    }
    log_calls++;
}

/* CRASH_SYSTEM - never returns in the image; longjmp out here. */
static jmp_buf   crash_jmp;
static int       crash_calls;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
    longjmp(crash_jmp, 1);
}

/* ==========================================================================
 * The unit under test
 * ========================================================================== */

#include "../allocate_pages.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

#define TEST_SEG   2
#define TEST_PPN   0x207   /* >= 0x200: MMAP_$MMAPE starts at ppn 0x200 */
#define TEST_PAGE  3        /* pmape->seg_offset */

static aote_t test_aote;

static uint32_t *segmap_entry_of(uint16_t seg, uint8_t page)
{
    return (uint32_t *)&PMAP_SEGMAP_ROW(seg)[page];
}

static void reset_mocks(void)
{
    int i;

    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(test_pft, 0, sizeof(test_pft));
    memset(&test_aote, 0, sizeof(test_aote));
    memset(&MMAP_$DATA, 0, sizeof(MMAP_$DATA));

    free_calls = pure_calls = 0;
    for (i = 0; i < MAX_ALLOC; i++) {
        free_result[i] = 0;
        pure_result[i] = 0;
    }
    memset(pure_ppns, 0, sizeof(pure_ppns));
    purifier_calls = 0;
    log_calls = 0;
    crash_calls = 0;

    AST_$ALLOC_TOO_FEW_CNT = 0;
    AST_$ALLOC_CNT = 0;
    NETLOG_$OK_TO_LOG = 0;
    ANON_$UID.high = 0x00000407;
    ANON_$UID.low = 0;

    /* Plenty of free memory, so the tail never wakes the purifier. */
    PMAP_$DATA.low_thresh = 0;

    /* One pure page, ppn 0x207, belonging to segment 2 page 3. */
    MMAPE_FOR_VPN(TEST_PPN)->segment = TEST_SEG;
    MMAPE_FOR_VPN(TEST_PPN)->seg_offset = TEST_PAGE;
    MMAPE_FOR_VPN(TEST_PPN)->flags2 = 0;
    MMAPE_FOR_VPN(TEST_PPN)->disk_addr = 0x00012345;

    *segmap_entry_of(TEST_SEG, TEST_PAGE) = SEGMAP_FLAG_IN_USE | TEST_PPN;

    AST_ASTE_ENTRY(TEST_SEG)->aote = &test_aote;
    AST_ASTE_ENTRY(TEST_SEG)->segment = 0xBEEF;
    AST_ASTE_ENTRY(TEST_SEG)->page_count = 9;

    test_aote.uid.high = 0x11112222;
    test_aote.uid.low  = 0x33334444;
    test_aote.dtm_high = 0xAAAA5555;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E00D56 / 0x00E00D6C-0x00E00D84 */
TEST(free_pool_satisfies_the_request)
{
    uint32_t ppns[8] = { 0 };
    int16_t got;

    free_result[0] = 4;

    got = ast_$allocate_pages(4, 1, ppns);

    ASSERT_EQ(4, got);
    ASSERT_EQ(1u, AST_$ALLOC_CNT);
    ASSERT_EQ(0u, AST_$ALLOC_TOO_FEW_CNT);
    ASSERT_EQ(1, free_calls);
    ASSERT_EQ(0, pure_calls);
    ASSERT_EQ(0, purifier_calls);
}

/* 0x00E00E52-0x00E00E68: not enough pages, so wake the purifier and retry */
TEST(short_allocation_wakes_the_purifier_and_retries)
{
    uint32_t ppns[8] = { 0 };
    int16_t got;

    free_result[0] = 0;
    free_result[1] = 2;

    got = ast_$allocate_pages(2, 1, ppns);

    ASSERT_EQ(2, got);
    ASSERT_EQ(1, purifier_calls);
    ASSERT_EQ((int8_t)0xFF, purifier_last);
    ASSERT_EQ(2, free_calls);
    ASSERT_EQ(0u, AST_$ALLOC_TOO_FEW_CNT);
}

/* 0x00E00E6C-0x00E00E70 */
TEST(partial_allocation_counts_a_failure)
{
    uint32_t ppns[8] = { 0 };
    int16_t got;

    /* min_count 1 is met by the first page, but two were asked for. */
    free_result[0] = 1;

    got = ast_$allocate_pages(2, 1, ppns);

    ASSERT_EQ(1, got);
    ASSERT_EQ(1u, AST_$ALLOC_TOO_FEW_CNT);
}

/* 0x00E00E02/0x00E00E08/0x00E00E10: the segment map entry is rewritten */
TEST(pure_page_rewrites_the_segment_map_entry)
{
    uint32_t ppns[8] = { 0 };
    uint32_t *entry = segmap_entry_of(TEST_SEG, TEST_PAGE);
    int16_t got;

    pure_result[0] = 1;
    pure_ppns[0][0] = TEST_PPN;
    *entry = SEGMAP_FLAG_IN_USE | 0x00700000u | TEST_PPN;

    got = ast_$allocate_pages(1, 1, ppns);

    ASSERT_EQ(1, got);
    /* bit 30 cleared, bits 22..0 replaced by pmape->disk_addr */
    ASSERT_EQ(0x00012345u, *entry);
    /* 0x00E00E2C */
    ASSERT_EQ(8, AST_ASTE_ENTRY(TEST_SEG)->page_count);
}

/* 0x00E00DD6-0x00E00DF4: a mismatched PPN crashes */
TEST(segment_map_mismatch_crashes)
{
    uint32_t ppns[8] = { 0 };

    pure_result[0] = 1;
    pure_ppns[0][0] = TEST_PPN;
    *segmap_entry_of(TEST_SEG, TEST_PAGE) = SEGMAP_FLAG_IN_USE | 0x11;

    if (setjmp(crash_jmp) == 0) {
        ast_$allocate_pages(1, 1, ppns);
        ASSERT_TRUE(0 && "CRASH_SYSTEM was not reached");
    }

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00050003, crash_status);
}

/* an entry already marked in transition crashes too */
TEST(segment_map_in_transition_crashes)
{
    uint32_t ppns[8] = { 0 };

    pure_result[0] = 1;
    pure_ppns[0][0] = TEST_PPN;
    *segmap_entry_of(TEST_SEG, TEST_PAGE) =
        SEGMAP_FLAG_IN_TRANS | SEGMAP_FLAG_IN_USE | TEST_PPN;

    if (setjmp(crash_jmp) == 0) {
        ast_$allocate_pages(1, 1, ppns);
        ASSERT_TRUE(0 && "CRASH_SYSTEM was not reached");
    }

    ASSERT_EQ(1, crash_calls);
}

/* and so does one that is still installed in the MMU */
TEST(segment_map_installed_crashes)
{
    uint32_t ppns[8] = { 0 };

    pure_result[0] = 1;
    pure_ppns[0][0] = TEST_PPN;
    *segmap_entry_of(TEST_SEG, TEST_PAGE) =
        SEGMAP_FLAG_INSTALLED | SEGMAP_FLAG_IN_USE | TEST_PPN;

    if (setjmp(crash_jmp) == 0) {
        ast_$allocate_pages(1, 1, ppns);
        ASSERT_TRUE(0 && "CRASH_SYSTEM was not reached");
    }

    ASSERT_EQ(1, crash_calls);
}

/* 0x00E00E30-0x00E00E44: logging is off unless NETLOG_$OK_TO_LOG < 0 */
TEST(no_logging_when_the_flag_is_clear)
{
    uint32_t ppns[8] = { 0 };

    pure_result[0] = 1;
    pure_ppns[0][0] = TEST_PPN;

    ast_$allocate_pages(1, 1, ppns);

    ASSERT_EQ(0, log_calls);
}

/*
 * The whole argument list of the nested helper, local object arm
 * (0x00E00D10-0x00E00D36).  Two pages are requested with a min_count of 2 so
 * that the min_count the helper reads uplevel is distinguishable from the
 * running `allocated` count.
 */
TEST(log_page_argument_list_local_object)
{
    uint32_t ppns[8] = { 0 };

    NETLOG_$OK_TO_LOG = (int8_t)-1;
    pure_result[0] = 2;
    pure_ppns[0][0] = TEST_PPN;
    pure_ppns[0][1] = TEST_PPN + 1;

    MMAPE_FOR_VPN(TEST_PPN + 1)->segment = TEST_SEG;
    MMAPE_FOR_VPN(TEST_PPN + 1)->seg_offset = TEST_PAGE + 1;
    *segmap_entry_of(TEST_SEG, TEST_PAGE + 1) =
        SEGMAP_FLAG_IN_USE | (TEST_PPN + 1);

    ast_$allocate_pages(2, 2, ppns);

    ASSERT_EQ(2, log_calls);
    ASSERT_EQ(4, log_kind[0]);
    /* the AOTE's own UID at aote+0x10 */
    ASSERT_EQ(0x11112222u, log_uid_high[0]);
    ASSERT_EQ(0x33334444u, log_uid_low[0]);
    /* (-0x8,A4) = AST_ASTE_ENTRY(seg)->segment */
    ASSERT_EQ(0xBEEF, log_p3[0]);
    /* (0x1,A2) = pmape->seg_offset */
    ASSERT_EQ(TEST_PAGE, log_p4[0]);
    /* D2w = the LOW word of the ppn */
    ASSERT_EQ(TEST_PPN, log_p5[0]);
    /* (-0xe,A3) = the parent's `allocated` BEFORE this page is counted */
    ASSERT_EQ(0, log_p6[0]);
    /* (0xa,A3) = the parent's min_count argument */
    ASSERT_EQ(2, log_p7[0]);
    ASSERT_EQ(0, log_p8[0]);
}

/* 0x00E00CD4-0x00E00CEC: the remote arm uses ANON_$UID.high + aote+0x2A */
TEST(log_page_remote_object_uses_anon_uid)
{
    uint32_t ppns[8] = { 0 };

    NETLOG_$OK_TO_LOG = (int8_t)-1;
    pure_result[0] = 1;
    pure_ppns[0][0] = TEST_PPN;
    MMAPE_FOR_VPN(TEST_PPN)->flags2 = MMAPE_FLAG2_ON_DISK;   /* bit 7 */

    ast_$allocate_pages(1, 1, ppns);

    ASSERT_EQ(1, log_calls);
    ASSERT_EQ(0x00000407u, log_uid_high[0]);
    ASSERT_EQ(0x00005555u, log_uid_low[0]);       /* the low word of dtm_high */
}

/*
 * 0x00E00CFC `move.w D2w,-(SP)`: only the LOW word of the 32-bit ppn is
 * logged.
 */
TEST(log_page_logs_the_low_word_of_the_ppn)
{
    uint32_t ppns[8] = { 0 };

    NETLOG_$OK_TO_LOG = (int8_t)-1;
    pure_result[0] = 1;
    pure_ppns[0][0] = TEST_PPN;

    ast_$allocate_pages(1, 1, ppns);

    ASSERT_EQ(1, log_calls);
    ASSERT_EQ((uint16_t)TEST_PPN, log_p5[0]);
}

/*
 * 0x00E00E48: `allocated` is bumped AFTER the log call, so a second page in
 * the same batch reports the first one.
 */
TEST(log_page_sees_the_running_allocated_count)
{
    uint32_t ppns[8] = { 0 };

    NETLOG_$OK_TO_LOG = (int8_t)-1;
    pure_result[0] = 2;
    pure_ppns[0][0] = TEST_PPN;
    pure_ppns[0][1] = TEST_PPN + 1;

    MMAPE_FOR_VPN(TEST_PPN + 1)->segment = TEST_SEG;
    MMAPE_FOR_VPN(TEST_PPN + 1)->seg_offset = TEST_PAGE + 1;
    *segmap_entry_of(TEST_SEG, TEST_PAGE + 1) =
        SEGMAP_FLAG_IN_USE | (TEST_PPN + 1);

    ast_$allocate_pages(2, 1, ppns);

    ASSERT_EQ(2, log_calls);
    ASSERT_EQ(0, log_p6[0]);
    ASSERT_EQ(1, log_p6[1]);
}

/* 0x00E00E74-0x00E00E9A: a low free pool wakes the purifier without waiting */
TEST(low_memory_wakes_the_purifier_at_the_tail)
{
    uint32_t ppns[8] = { 0 };

    free_result[0] = 1;
    PMAP_$DATA.low_thresh = 100;

    ast_$allocate_pages(1, 1, ppns);

    ASSERT_EQ(1, purifier_calls);
    ASSERT_EQ(0, purifier_last);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("ast_$allocate_pages tests:\n");

    RUN_TEST(free_pool_satisfies_the_request);
    RUN_TEST(short_allocation_wakes_the_purifier_and_retries);
    RUN_TEST(partial_allocation_counts_a_failure);
    RUN_TEST(pure_page_rewrites_the_segment_map_entry);
    RUN_TEST(segment_map_mismatch_crashes);
    RUN_TEST(segment_map_in_transition_crashes);
    RUN_TEST(segment_map_installed_crashes);
    RUN_TEST(no_logging_when_the_flag_is_clear);
    RUN_TEST(log_page_argument_list_local_object);
    RUN_TEST(log_page_remote_object_uses_anon_uid);
    RUN_TEST(log_page_logs_the_low_word_of_the_ppn);
    RUN_TEST(log_page_sees_the_running_allocated_count);
    RUN_TEST(low_memory_wakes_the_purifier_at_the_tail);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
