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
 *   - the segment map (PMAP_$SEGMAP, image 0xED5000) takes a 1-based
 *     segment index, i.e. PMAP_SEGMAP_ROW(seg)[page] is 0xED5000 +
 *     (seg - 1)*0x80 + page*4.
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

#define TEST_SEGMENTS   8
#define TEST_PAGES      64

static uint32_t          pft_store[TEST_PAGES];

/*
 * The MMAP_ module data block (`D E23284 MMAP_ size = AA8').  MMAP_$WSL,
 * MMAP_$WSL_HI_MARK, MMAP_$STEAL_CNT and MMAP_$PAGEABLE_PAGES are all fields
 * of this one object (mmap/mmap.h), so the daemon's whole MMAP_ world is
 * this single definition.
 */
MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);

MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
#define SAU2_PFT_BASE pft_store   /* the SAU2 PFT (arch/m68k/sau2/hw.h) */

MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);
uint32_t TIME_$CLOCKH;

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
uint16_t PROC1_$CURRENT;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
int8_t   NETLOG_$OK_TO_LOG;
int8_t   NETWORK_$DISKLESS;
log_state_t LOG_$STATE;
uid_t    UID_$NIL;
#include "ast/ast.h"
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{ (void)ecs; (void)vals; return 0; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; }
void ML_$LOCK(int16_t id) { (void)id; }
void ML_$UNLOCK(int16_t id) { (void)id; }
void (PROC1_$SET_LOCK)(uint32_t id_slot) { uint16_t id = (uint16_t)ARCH_PASCAL_SLOT_WORD(id_slot); (void)id; (void)id; }
void PMAP_$INIT_TIMERS(void) { }
void CAL_$SHUTDOWN(status_$t *st) { (void)st; }
void CRASH_SYSTEM(const status_$t *st) { (void)st; }
void MMAP_$AVAIL(uint32_t vpn) { (void)vpn; }
void MMAP_$UNAVAIL_REMOV(uint32_t vpn, boolean f) { (void)vpn; (void)f; }
void MMAP_$GET_IMPURE(uint16_t w, uint32_t *v, int8_t a, uint16_t m,
                      uint32_t *s, uint16_t *r)
{ (void)w; (void)v; (void)a; (void)m; *s = 0; *r = 0; }
void DISK_$GET_QBLKS(int16_t c, uint32_t *h, uint32_t *t)
{ (void)c; *h = 0; *t = 0; }
void DISK_$RTN_QBLKS(int16_t c, uint32_t h, uint32_t t) { (void)c; (void)h; (void)t; }
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
void pmap_$write_complete(int32_t vpn, status_$t *sp) { (void)vpn; (void)sp; }
void pmap_$write_page(uint32_t vpn, status_$t *st, int8_t f)
{ (void)vpn; (void)f; *st = 0; }

static void reset_mocks(void)
{
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(pft_store, 0, sizeof(pft_store));

    scan_calls = 0;
    purge_calls = 0;
    memset(scan_slot, 0, sizeof(scan_slot));
    memset(scan_pages, 0, sizeof(scan_pages));
    memset(purge_slot, 0, sizeof(purge_slot));

    MMAP_WSL_HI_MARK = 7;
    PMAP_$DATA.ws_interval = 10;
    PMAP_$DATA.idle_interval = 100;
    TIME_$CLOCKH = 1000;
    PMAP_$DATA.ws_random_seed = 1;
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

    MMAP_WSL[5].page_count = 4;
    MMAP_WSL[5].owner = 99;
    MMAP_WSL[7].page_count = 4;
    MMAP_WSL[7].owner = 99;
    MMAP_WSL[7].ws_timestamp = 0;

    ASSERT_TRUE(pmap_$purifier_ws_scan_pass(50, &prev_steal) < 0);

    ASSERT_EQ(1, scan_calls);
    ASSERT_EQ(7, scan_slot[0]);
    ASSERT_EQ(0, MMAP_WSL[7].owner);              /* 0x00E13E82 */
    ASSERT_EQ(1000, MMAP_WSL[7].ws_timestamp);    /* 0x00E13E86 */
    ASSERT_EQ(99, MMAP_WSL[5].owner);             /* never reached */
    ASSERT_EQ(1, prev_steal);                      /* 0x00E13E3E */
}

/* 0x00E13E9A/0x00E13EA0: an idle working set is purged outright. */
static void test_idle_slot_is_purged(void)
{
    int32_t prev_steal = 0;

    MMAP_WSL[6].page_count = 4;
    MMAP_WSL[6].owner = 0;                 /* not overdue */
    MMAP_WSL[6].pri_timestamp = 800;       /* < 1000 - 100 */

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
    MMAP_WSL[5].page_count = 3;
    MMAP_WSL[5].ws_floor = 3;
    MMAP_WSL[5].pri_timestamp = 1000;
    MMAP_WSL[6].page_count = 2;
    MMAP_WSL[6].ws_floor = 9;
    MMAP_WSL[6].pri_timestamp = 1000;

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

    MMAP_WSL[5].page_count = 3;
    MMAP_WSL[5].ws_floor = 3;
    MMAP_WSL[5].pri_timestamp = 1000;

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

    MMAP_WSL[5].page_count = 8;
    MMAP_WSL[5].ws_floor = 0;
    MMAP_WSL[5].pri_timestamp = 1000;
    MMAP_WSL[7].page_count = 8;
    MMAP_WSL[7].ws_floor = 0;
    MMAP_WSL[7].pri_timestamp = 1000;

    PMAP_$DATA.ws_random_seed = 0;      /* the draw stays 0, so target == 0 */

    ASSERT_TRUE(pmap_$purifier_ws_scan_pass(50, &prev_steal) < 0);
    ASSERT_EQ(1, scan_calls);
    ASSERT_EQ(7, scan_slot[0]);
}

/*
 * The segment index is 1-based: PMAP_SEGMAP_ROW(seg)[page] must land
 * (seg - 1)*0x80 + page*4 bytes past the block (image 0xED5000), i.e.
 * 0xED5000 + seg*0x80 - 0x80 as "lea (-0x80,A0,...)" computes it.
 */
static void test_segmap_indexing_is_one_based(void)
{
    char *base = (char *)&PMAP_$SEGMAP;

    ASSERT_EQ(0x00, (char *)&PMAP_SEGMAP_ROW(1)[0] - base);
    ASSERT_EQ(0x04, (char *)&PMAP_SEGMAP_ROW(1)[1] - base);
    ASSERT_EQ(0x80, (char *)&PMAP_SEGMAP_ROW(2)[0] - base);
    ASSERT_EQ(4, (long)sizeof(pmap_segmap_entry_t));
    ASSERT_EQ(0x80, (long)sizeof(pmap_segmap_row_t));

    PMAP_SEGMAP_ROW(3)[2].flags |= PMAP_SEGMAP_WRITING;
    ASSERT_EQ(0x80, (uint8_t)base[2 * 0x80 + 2 * 4]);
    PMAP_SEGMAP_ROW(3)[2].flags = 0;
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

/* DISK_$DIAG, DISK_$DO_CHKSUM and the module exclusion lock are cells of the
 * DISK_ module block (disk/disk.h), so the host build provides the block. */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];
