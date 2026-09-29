/*
 * pmap/test/test_callbacks.c - Unit tests for PMAP_$T_PURIF_CALLBACK
 * (0x00E143CC), PMAP_$WS_SCAN_CALLBACK (0x00E144F6), PMAP_$WAKE_PURIFIER
 * (0x00E13A18), pmap_$wait_in_transit (0x00E12D38) and PMAP_$PURGE_WS
 * (0x00E146B4)
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "ec/ec.h"
#include "mmap/mmap.h"
#include "misc/misc.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

mmap_globals_t MMAP_GLOBALS_STORAGE;
MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);
uint32_t TIME_$CLOCKH;
ec_$eventcount_t AST_$PMAP_IN_TRANS_EC;
status_$t status_$t_00e145ec = 0x00050010;

static int locks, unlocks, purges, scans, advances, waitns, free_wsls;
static uint16_t purge_slot, scan_slot, free_slot;
static int16_t scan_mode;
static uint32_t scan_needed, scan_p4;
static ec_$eventcount_t *advanced[4];
static ec_$eventcount_t *waitn_ec;
static int32_t waitn_val;
static int lock_depth_at_wait;
static jmp_buf crash_jmp;
static status_$t crash_status;

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlocks++; }
void MMAP_$PURGE(uint16_t s) { purges++; purge_slot = s; }
void MMAP_$FREE_WSL(uint16_t pid) { free_wsls++; free_slot = pid; }
uint32_t MMAP_$WS_SCAN(uint16_t s, int16_t mode, uint32_t needed, uint32_t p4)
{ scans++; scan_slot = s; scan_mode = mode; scan_needed = needed; scan_p4 = p4; return 0; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { if (advances < 4) advanced[advances] = ec; advances++; }
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *vals, int16_t n)
{ (void)n; waitns++; waitn_ec = ecs[0]; waitn_val = vals[0]; lock_depth_at_wait = locks - unlocks; return 0; }
void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }

#include "../t_purif_callback.c"
#include "../ws_scan_callback.c"
#include "../wake_purifier.c"
#include "../wait_in_transit.c"
#include "../purge_ws.c"

static void reset(void)
{
    memset(&MMAP_GLOBALS_STORAGE, 0, sizeof MMAP_GLOBALS_STORAGE);
    locks = unlocks = purges = scans = advances = waitns = free_wsls = 0;
    PMAP_$DATA.ws_interval = 5;
    TIME_$CLOCKH = 0x10000;
    PMAP_$DATA.current_slot = 10;
    PMAP_$DATA.t_pur_scans = 0;
}

TEST(t_purif_slot_wraps_and_limit)
{
    reset();
    PMAP_$DATA.current_slot = 0x45;
    MMAP_$PAGEABLE_PAGES = 0x4000;
    PMAP_$DATA.t_pur_scans = 3;
    PMAP_$T_PURIF_CALLBACK();
    ASSERT_EQ(5, PMAP_$DATA.current_slot);
    ASSERT_EQ(4, PMAP_$DATA.t_pur_scans);
    ASSERT_EQ(0x4000 - 0x800, MMAP_WSL[5].max_pages);   /* capped at 0x800 */
    ASSERT_EQ(0, locks);                                 /* no pages: done */
    ASSERT_EQ(0, advances);
    reset();
    MMAP_$PAGEABLE_PAGES = 0x1000;
    MMAP_WSL[11].flags = 0x20;                           /* bit 13 of the word */
    MMAP_WSL[11].max_pages = 77;
    PMAP_$T_PURIF_CALLBACK();
    ASSERT_EQ(11, PMAP_$DATA.current_slot);
    ASSERT_EQ(77, MMAP_WSL[11].max_pages);
}

TEST(t_purif_purges_idle_slot)
{
    reset();
    MMAP_WSL[11].page_count = 4;
    MMAP_WSL[11].ws_timestamp = 0;
    MMAP_WSL[11].pri_timestamp = 0x10000 - 0x1CA - 1;
    PMAP_$T_PURIF_CALLBACK();
    ASSERT_EQ(1, purges);
    ASSERT_EQ(11, purge_slot);
    ASSERT_EQ(0, scans);
    ASSERT_EQ(1, advances);
    ASSERT_EQ((unsigned long)&PMAP_$DATA.pages_ec, (unsigned long)advanced[0]);
    ASSERT_EQ(1, locks);
    ASSERT_EQ(1, unlocks);
}

TEST(t_purif_full_and_partial_scan_modes)
{
    reset();
    /* recently scanned (ws stamp newer than the cutoff): nothing */
    MMAP_WSL[11].page_count = 4;
    MMAP_WSL[11].ws_timestamp = 0x10000 - 0x100;
    PMAP_$T_PURIF_CALLBACK();
    ASSERT_EQ(0, scans);
    /* old scan, owner over the interval -> full scan (mode 0) */
    reset();
    MMAP_WSL[11].page_count = 4;
    MMAP_WSL[11].ws_timestamp = 0x1000;
    MMAP_WSL[11].owner = 6;
    PMAP_$T_PURIF_CALLBACK();
    ASSERT_EQ(1, scans);
    ASSERT_EQ(11, scan_slot);
    ASSERT_EQ(0, scan_mode);
    ASSERT_EQ(0x3FFFFF, scan_needed);
    ASSERT_EQ(0, MMAP_WSL[11].owner);
    ASSERT_EQ(0x10000, MMAP_WSL[11].ws_timestamp);
    ASSERT_EQ(4, MMAP_WSL[11].scan_pos);
    /* old scan, owner within the interval, no newer priority -> partial */
    reset();
    MMAP_WSL[11].page_count = 4;
    MMAP_WSL[11].ws_timestamp = 0x1000;
    MMAP_WSL[11].pri_timestamp = 0x800;
    MMAP_WSL[11].owner = 2;
    PMAP_$T_PURIF_CALLBACK();
    ASSERT_EQ(1, scans);
    ASSERT_EQ((uint16_t)0xFF00, (uint16_t)scan_mode);   /* st -(SP) */
    ASSERT_EQ(2, MMAP_WSL[11].owner);
    ASSERT_EQ(0x1000, MMAP_WSL[11].ws_timestamp);
}

TEST(ws_scan_callback_ages_process_and_wired_pool)
{
    uint16_t rec[4] = { 0, 3, 0, 0 };
    uint32_t *elem = (uint32_t *)rec;
    time_$callback_arg_t cell = &elem;
    reset();
    MMAP_$WS_OWNER[3 - 1] = 9;
    MMAP_WSL[9].owner = 4;
    MMAP_WSL[9].page_count = 20;
    MMAP_WSL[5].ws_timestamp = 0x10000 - 9;
    MMAP_WSL[5].owner = 4;
    MMAP_WSL[5].page_count = 33;
    PMAP_$WS_SCAN_CALLBACK(cell);
    ASSERT_EQ(2, scans);
    ASSERT_EQ(0, MMAP_WSL[9].owner);
    ASSERT_EQ(20, MMAP_WSL[9].scan_pos);
    ASSERT_EQ(0x10000, MMAP_WSL[9].ws_timestamp);
    ASSERT_EQ(5, scan_slot);                              /* the wired pool last */
    ASSERT_EQ(0, MMAP_WSL[5].owner);
    ASSERT_EQ(33, MMAP_WSL[5].scan_pos);
    ASSERT_EQ(0x10000, MMAP_WSL[5].pri_timestamp);
    ASSERT_EQ(1, locks);
    ASSERT_EQ(1, unlocks);
}

TEST(ws_scan_callback_counts_only)
{
    uint16_t rec[4] = { 0, 3, 0, 0 };
    uint32_t *elem = (uint32_t *)rec;
    time_$callback_arg_t cell = &elem;
    reset();
    MMAP_$WS_OWNER[3 - 1] = 9;
    MMAP_WSL[9].owner = 1;
    MMAP_WSL[5].ws_timestamp = 0x10000;                   /* not yet due */
    PMAP_$WS_SCAN_CALLBACK(cell);
    ASSERT_EQ(0, scans);
    ASSERT_EQ(2, MMAP_WSL[9].owner);
    ASSERT_EQ(0, MMAP_WSL[5].owner);
}

TEST(ws_scan_callback_bad_pid_crashes)
{
    uint16_t rec[4] = { 0, 0x41, 0, 0 };
    uint32_t *elem = (uint32_t *)rec;
    time_$callback_arg_t cell = &elem;
    reset();
    if (setjmp(crash_jmp) == 0) {
        PMAP_$WS_SCAN_CALLBACK(cell);
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(0x00050010, crash_status);
}

TEST(wake_purifier)
{
    reset();
    PMAP_$DATA.pages_ec.value = 40;
    PMAP_$WAKE_PURIFIER(0);
    ASSERT_EQ(1, advances);
    ASSERT_EQ((unsigned long)&PMAP_$DATA.l_purifier_ec, (unsigned long)advanced[0]);
    ASSERT_EQ(0, waitns);
    reset();
    MMAP_WSL[4].page_count = 1;
    PMAP_$WAKE_PURIFIER(-1);
    ASSERT_EQ(2, advances);
    ASSERT_EQ((unsigned long)&PMAP_$DATA.r_purifier_ec, (unsigned long)advanced[1]);
    ASSERT_EQ(1, waitns);
    ASSERT_EQ((unsigned long)&PMAP_$DATA.pages_ec, (unsigned long)waitn_ec);
    ASSERT_EQ(41, waitn_val);
    ASSERT_EQ(-1, lock_depth_at_wait);                    /* unlocked while waiting */
    ASSERT_EQ(1, locks);
}

TEST(wait_in_transit)
{
    reset();
    AST_$PMAP_IN_TRANS_EC.value = 7;
    pmap_$wait_in_transit();
    ASSERT_EQ(1, waitns);
    ASSERT_EQ((unsigned long)&AST_$PMAP_IN_TRANS_EC, (unsigned long)waitn_ec);
    ASSERT_EQ(8, waitn_val);
    ASSERT_EQ(-1, lock_depth_at_wait);
    ASSERT_EQ(1, locks);
}

TEST(purge_ws_both_arms)
{
    reset();
    MMAP_$WS_OWNER[6 - 1] = 12;
    PMAP_$PURGE_WS(6, (int16_t)0xFF00);
    ASSERT_EQ(1, purges);
    ASSERT_EQ(12, purge_slot);
    PMAP_$PURGE_WS(6, 0);
    ASSERT_EQ(1, free_wsls);
    ASSERT_EQ(6, free_slot);
    ASSERT_EQ(2, locks);
    ASSERT_EQ(2, unlocks);
}

int main(void)
{
    printf("test_callbacks:\n");
    RUN_TEST(t_purif_slot_wraps_and_limit);
    RUN_TEST(t_purif_purges_idle_slot);
    RUN_TEST(t_purif_full_and_partial_scan_modes);
    RUN_TEST(ws_scan_callback_ages_process_and_wired_pool);
    RUN_TEST(ws_scan_callback_counts_only);
    RUN_TEST(ws_scan_callback_bad_pid_crashes);
    RUN_TEST(wake_purifier);
    RUN_TEST(wait_in_transit);
    RUN_TEST(purge_ws_both_arms);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
