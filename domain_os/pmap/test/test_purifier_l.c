/*
 * pmap/test/test_purifier_l.c - Unit tests for PMAP_$PURIFIER_L
 *
 * Bead source-qlns.  The interesting logic of the daemon is the working-set
 * scan-and-select pass, which was lifted into the file-static
 * pmap_$purifier_ws_scan_pass (0x00E13E34-0x00E13F4E).  The real .c is
 * #included at the bottom so the real code runs.
 *
 * What is pinned here:
 *   - the slot walk really runs from MMAP_WSL_HI_MARK down to 5 inclusive
 *     (1-based-ish bounds at 0x00E13E4E/0x00E13EC6), not a single pass;
 *   - the "overdue" arm (0x00E13E82) resets the age and rescans;
 *   - the "idle" arm (0x00E13EA0) purges;
 *   - only pages above ws_hdr_t.ws_floor count as candidates, unless the
 *     clean pools are completely empty (0x00E13EB0);
 *   - a pass with nothing stealable returns false so the caller abandons
 *     the low-memory loop (0x00E13ECC -> 0x00E13FAC);
 *   - the proportional draw picks the working set the accumulator crosses
 *     (0x00E13F2A);
 *   - the segment map base is 0xED4F80 with a 1-based segment index, i.e.
 *     PMAP_SEGMAP[seg][page] is seg*0x80 + page*4 BELOW 0xED5000.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_mocks(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((long)(expected) != (long)(actual)) { \
        printf("FAILED\n    Expected: %ld, Got: %ld at line %d\n", \
               (long)(expected), (long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Assertion failed at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "pmap/pmap_internal.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"

/*
 * ========================================================================
 * Backing store for the globals the daemon reaches through macros
 * ========================================================================
 */

#define TEST_WSL_SLOTS  16
#define TEST_SEGMENTS   8
#define TEST_PAGES      64

static ws_hdr_t          wsl_store[TEST_WSL_SLOTS];
static pmap_segmap_row_t segmap_store[TEST_SEGMENTS];
static mmape_t           mmape_store[TEST_PAGES];
static uint32_t          pft_store[TEST_PAGES];

ws_hdr_t          *mmap_wsl = wsl_store;
pmap_segmap_row_t *pmap_segmap = segmap_store;
mmape_t           *mmap_mmape_base = mmape_store;
uint32_t          *mmu_pft_base = pft_store;

uint16_t mmap_wsl_hi_mark;      /* MMAP_WSL_HI_MARK on ARCH_HOST */
uint16_t PMAP_$WS_INTERVAL;
uint32_t PMAP_$IDLE_INTERVAL;
uint32_t TIME_$CLOCKH;
uint16_t DAT_00e254e2;

/*
 * ========================================================================
 * Mocks
 * ========================================================================
 */

#define MAX_CALLS 8
static int scan_calls;
static uint16_t scan_slot[MAX_CALLS];
static uint32_t scan_pages[MAX_CALLS];
static int purge_calls;
static uint16_t purge_slot[MAX_CALLS];

uint32_t MMAP_$WS_SCAN(uint16_t wsl_index, int16_t mode, uint32_t pages_needed,
                       uint32_t param4)
{
    (void)mode;
    (void)param4;
    if (scan_calls < MAX_CALLS) {
        scan_slot[scan_calls] = wsl_index;
        scan_pages[scan_calls] = pages_needed;
    }
    scan_calls++;
    return 0;
}

void MMAP_$PURGE(uint16_t wsl_index)
{
    if (purge_calls < MAX_CALLS) {
        purge_slot[purge_calls] = wsl_index;
    }
    purge_calls++;
}

/*
 * The rest of the daemon's world.  PMAP_$PURIFIER_L itself is never called
 * by these tests (it never returns), but the translation unit still has to
 * link, so every external it touches gets a stub here.
 */
uint16_t PMAP_$LOW_THRESH, PMAP_$MID_THRESH, PMAP_$MAX_WS_INTERVAL;
uint16_t PMAP_$MIN_WS_INTERVAL, PMAP_$WS_SCAN_DELTA;
uint32_t PMAP_$PUR_L_CNT;
int8_t   PMAP_$SHUTTING_DOWN_FLAG;
clock_t  PMAP_$SHORT_WAIT_DELAY;
ec_$eventcount_t PMAP_$L_PURIFIER_EC, PMAP_$PAGES_EC;
uint32_t MMAP_$STEAL_CNT, MMAP_$PAGEABLE_PAGES_LOWER_LIMIT;
uint16_t PROC1_$CURRENT;
uint32_t PROC_STATS_BASE[PROC1_MAX_PROCESSES * 4];
int8_t   NETLOG_$OK_TO_LOG;
int8_t   NETWORK_$DISKLESS;
int8_t   DISK_$DO_CHKSUM;
log_state_t LOG_$STATE;
uid_t    UID_$NIL;
struct aste_t *ast_aste_base;
ec_$eventcount_t ast_pmap_in_trans_ec;

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{ (void)ecs; (void)vals; return 0; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; }
void ML_$LOCK(int16_t id) { (void)id; }
void ML_$UNLOCK(int16_t id) { (void)id; }
void PROC1_$SET_LOCK(uint16_t id) { (void)id; }
void PMAP_$INIT_TIMERS(void) { }
void CAL_$SHUTDOWN(status_$t *st) { (void)st; }
void CRASH_SYSTEM(const status_$t *st) { (void)st; }
void MMAP_$AVAIL(uint32_t vpn) { (void)vpn; }
void MMAP_$UNAVAIL_REMOV(uint32_t vpn, boolean f) { (void)vpn; (void)f; }
void MMAP_$GET_IMPURE(uint16_t w, uint32_t *v, int8_t a, uint16_t m,
                      uint32_t *s, uint16_t *r)
{ (void)w; (void)v; (void)a; (void)m; *s = 0; *r = 0; }
void DISK_$GET_QBLKS(int16_t c, int32_t *h, uint32_t *t)
{ (void)c; *h = 0; *t = 0; }
void DISK_$RTN_QBLKS(int16_t c, int32_t h, uint32_t t) { (void)c; (void)h; (void)t; }
void DISK_$WRITE_MULTI(int8_t f, void *l, status_$t *st) { (void)f; (void)l; *st = 0; }
void NETLOG_$LOG_IT(uint16_t k, uint32_t *u, uint16_t a, uint16_t b,
                    uint16_t c, uint16_t d, uint16_t e, uint16_t f)
{ (void)k; (void)u; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
uint32_t LOG_$UPDATE(void) { return 0; }
unsigned long M$DIU$LLW(unsigned long n, unsigned short d) { return d ? n / d : 0; }
void TIME_$CLOCK(clock_t *c) { c->high = 0; c->low = 0; }
void TIME_$ABS_CLOCK(clock_t *c) { c->high = 0; c->low = 0; }
void TIME_$WAIT(uint16_t *t, clock_t *d, status_$t *st)
{ (void)t; (void)d; *st = 0; }
void pmap_$fill_write_qblks(int32_t *p, uint32_t *q, int16_t c)
{ (void)p; (void)q; (void)c; }
void pmap_$write_complete(int32_t vpn, void *sp) { (void)vpn; (void)sp; }
void pmap_$write_page(uint32_t vpn, status_$t *st, int8_t f)
{ (void)vpn; (void)f; *st = 0; }

static void reset_mocks(void)
{
    memset(wsl_store, 0, sizeof(wsl_store));
    memset(segmap_store, 0, sizeof(segmap_store));
    memset(mmape_store, 0, sizeof(mmape_store));
    memset(pft_store, 0, sizeof(pft_store));

    scan_calls = 0;
    purge_calls = 0;
    memset(scan_slot, 0, sizeof(scan_slot));
    memset(scan_pages, 0, sizeof(scan_pages));
    memset(purge_slot, 0, sizeof(purge_slot));

    MMAP_WSL_HI_MARK = 7;
    PMAP_$WS_INTERVAL = 10;
    PMAP_$IDLE_INTERVAL = 100;
    TIME_$CLOCKH = 1000;
    DAT_00e254e2 = 1;
}

/*
 * ========================================================================
 * Tests
 * ========================================================================
 *
 * Forward declaration of the file-static helper: the real .c is included
 * at the bottom of this file so the definitions come after the tests.
 */
static boolean pmap_$purifier_ws_scan_pass(uint32_t total_pages,
                                           int32_t *prev_steal);

/*
 * 0x00E13E78: a working set whose age has passed PMAP_$WS_INTERVAL is
 * scanned in full and its age reset.  The walk starts at the high-water
 * mark, so slot 7 wins over slot 5 even though both are overdue.
 */
static void test_overdue_slot_is_rescanned_from_the_top(void)
{
    int32_t prev_steal = 0;

    wsl_store[5].page_count = 4;
    wsl_store[5].owner = 99;
    wsl_store[7].page_count = 4;
    wsl_store[7].owner = 99;
    wsl_store[7].ws_timestamp = 0;

    ASSERT_TRUE(pmap_$purifier_ws_scan_pass(50, &prev_steal) < 0);

    ASSERT_EQ(1, scan_calls);
    ASSERT_EQ(7, scan_slot[0]);
    ASSERT_EQ(0, wsl_store[7].owner);              /* 0x00E13E82 */
    ASSERT_EQ(1000, wsl_store[7].ws_timestamp);    /* 0x00E13E86 */
    ASSERT_EQ(99, wsl_store[5].owner);             /* never reached */
    ASSERT_EQ(1, prev_steal);                      /* 0x00E13E3E */
}

/* 0x00E13E9A/0x00E13EA0: an idle working set is purged outright. */
static void test_idle_slot_is_purged(void)
{
    int32_t prev_steal = 0;

    wsl_store[6].page_count = 4;
    wsl_store[6].owner = 0;                 /* not overdue */
    wsl_store[6].pri_timestamp = 800;       /* < 1000 - 100 */

    ASSERT_TRUE(pmap_$purifier_ws_scan_pass(50, &prev_steal) < 0);

    ASSERT_EQ(1, purge_calls);
    ASSERT_EQ(6, purge_slot[0]);
    ASSERT_EQ(0, scan_calls);
}

/*
 * 0x00E13ECA: when no working set has a page above its floor the pass
 * returns false, which is what makes the caller abandon the low-memory loop
 * instead of spinning.
 */
static void test_no_candidate_returns_false(void)
{
    int32_t prev_steal = 0;

    /* Every set is entirely at or below its floor. */
    wsl_store[5].page_count = 3;
    wsl_store[5].ws_floor = 3;
    wsl_store[5].pri_timestamp = 1000;
    wsl_store[6].page_count = 2;
    wsl_store[6].ws_floor = 9;
    wsl_store[6].pri_timestamp = 1000;

    ASSERT_TRUE(pmap_$purifier_ws_scan_pass(50, &prev_steal) == 0);
    ASSERT_EQ(0, scan_calls);
    ASSERT_EQ(0, purge_calls);
}

/*
 * 0x00E13EB0: the floor test is skipped entirely when the clean pools are
 * empty (total_pages == 0), so a set at its floor still becomes a candidate.
 */
static void test_empty_pools_ignore_the_floor(void)
{
    int32_t prev_steal = 0;

    wsl_store[5].page_count = 3;
    wsl_store[5].ws_floor = 3;
    wsl_store[5].pri_timestamp = 1000;

    /* total_pages != 0: nothing stealable. */
    ASSERT_TRUE(pmap_$purifier_ws_scan_pass(50, &prev_steal) == 0);
    ASSERT_EQ(0, scan_calls);

    /* total_pages == 0: the same set is now a candidate. */
    prev_steal = 0;
    ASSERT_TRUE(pmap_$purifier_ws_scan_pass(0, &prev_steal) < 0);
    ASSERT_EQ(1, scan_calls);
    ASSERT_EQ(5, scan_slot[0]);
    ASSERT_EQ(1, scan_pages[0]);           /* 0x00E13F2E asks for one page */
}

/*
 * 0x00E13EE8-0x00E13F2E: the second walk sums the same candidates from the
 * top down and stops at the first set the running total passes.  With the
 * seed forced so that target == 0, that is the highest-numbered candidate.
 */
static void test_selection_takes_the_first_slot_crossed(void)
{
    int32_t prev_steal = 0;

    wsl_store[5].page_count = 8;
    wsl_store[5].ws_floor = 0;
    wsl_store[5].pri_timestamp = 1000;
    wsl_store[7].page_count = 8;
    wsl_store[7].ws_floor = 0;
    wsl_store[7].pri_timestamp = 1000;

    DAT_00e254e2 = 0;      /* the draw stays 0, so target == 0 */

    ASSERT_TRUE(pmap_$purifier_ws_scan_pass(50, &prev_steal) < 0);
    ASSERT_EQ(1, scan_calls);
    ASSERT_EQ(7, scan_slot[0]);
}

/*
 * The segment map base is 0xED4F80 and the segment index is 1-based:
 * PMAP_SEGMAP[seg][page] must land seg*0x80 + page*4 bytes past the base,
 * which is 0x80 BELOW the 0xED5000 the old code used for segment 1.
 */
static void test_segmap_indexing_is_one_based(void)
{
    char *base = (char *)&PMAP_SEGMAP[0][0];

    ASSERT_EQ(0x80, (char *)&PMAP_SEGMAP[1][0] - base);
    ASSERT_EQ(0x84, (char *)&PMAP_SEGMAP[1][1] - base);
    ASSERT_EQ(4, (long)sizeof(pmap_segmap_entry_t));
    ASSERT_EQ(0x80, (long)sizeof(pmap_segmap_row_t));

    PMAP_SEGMAP[3][2].flags |= PMAP_SEGMAP_WRITING;
    ASSERT_EQ(0x80, (uint8_t)base[3 * 0x80 + 2 * 4]);
}

int main(void)
{
    printf("Running PMAP_$PURIFIER_L working-set scan tests...\n\n");

    RUN_TEST(overdue_slot_is_rescanned_from_the_top);
    RUN_TEST(idle_slot_is_purged);
    RUN_TEST(no_candidate_returns_false);
    RUN_TEST(empty_pools_ignore_the_floor);
    RUN_TEST(selection_takes_the_first_slot_crossed);
    RUN_TEST(segmap_indexing_is_one_based);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

/* The real implementation under test. */
#include "../purifier_l.c"
