/*
 * osinfo/test/test_get_mmap.c - unit tests for OSINFO_$GET_MMAP (0x00E5C694)
 *
 * The routine reads the request flags from byte 1 of the record its first
 * argument points at and runs one block per flag.  The tests pin the
 * details recovered from the disassembly: the ASID clamp of the set
 * operations, PMAP_$PURGE_WS's boolean landing in the high byte of its
 * word slot, the FIND_PAGE early exits, the GET_WS_LIST pairing of
 * MMAP_$WS_OWNER[i] with PROC1_$DATA.type[i + 1] (A1 = 0xE2612E, `move.w
 * (-0x2,A1)`, while PROC1_$DATA.type[0] sits at 0xE2612A) and the 0x40 clamp,
 * and GET_WS_INFO's three longwords per WSL entry from MMAP_$WSL[5] on.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "osinfo/osinfo_internal.h"
#include "mmap/mmap.h"
#include "pmap/pmap.h"
#include "ast/ast.h"
#include "proc2/proc2.h"

/* ------------------------------------------------------------------ */
/* The globals the routine reads and writes                            */
/* ------------------------------------------------------------------ */

MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);
mmape_t  *mmap_mmape_base;
aste_t   *ast_aste_base;
uint32_t  ast_ws_flt_cnt, ast_page_flt_cnt, ast_alloc_too_few_cnt, ast_alloc_cnt;
MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);
uint16_t  PROC1_$CURRENT;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int      mock_set_ws_max_calls;
static uint16_t mock_set_ws_max_index;
static uint32_t mock_set_ws_max_pages;
static int      mock_purge_calls;
static int16_t  mock_purge_index;
static int16_t  mock_purge_flags;
static int      mock_get_pid_calls;
static uid_t   *mock_get_pid_uid;

void MMAP_$SET_WS_MAX(uint16_t wsl_index, uint32_t max_pages, status_$t *status)
{
    mock_set_ws_max_calls++;
    mock_set_ws_max_index = wsl_index;
    mock_set_ws_max_pages = max_pages;
    *status = 0x00060009;
}

void PMAP_$PURGE_WS(int16_t index, int16_t flags)
{
    mock_purge_calls++;
    mock_purge_index = index;
    mock_purge_flags = flags;
}

uint16_t PROC2_$GET_PID(uid_t *proc_uid, status_$t *status_ret)
{
    mock_get_pid_calls++;
    mock_get_pid_uid = proc_uid;
    *status_ret = status_$ok;
    return 0x1234;
}

#include "../get_mmap.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

static mmape_t mmape_pages[16];
static aste_t  aste_table[4];
static aote_t  aote_entries[4];
static uint8_t flags_rec[4];
static osinfo_global_info_t info;
static osinfo_paging_counters_t counters;
static uint16_t ws_list[0x50 * 2];
static uint32_t ws_data[70 * 3];
static uid_t    uid_out;
static status_$t status;

static void reset_state(void)
{
    memset(&MMAP_$DATA, 0, sizeof(MMAP_$DATA));
    memset(mmape_pages, 0, sizeof(mmape_pages));
    memset(aste_table, 0, sizeof(aste_table));
    memset(aote_entries, 0, sizeof(aote_entries));
    memset(flags_rec, 0, sizeof(flags_rec));
    memset(&info, 0, sizeof(info));
    memset(&counters, 0, sizeof(counters));
    memset(ws_list, 0xEE, sizeof(ws_list));
    memset(ws_data, 0xEE, sizeof(ws_data));
    memset(&uid_out, 0, sizeof(uid_out));
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));
    mmap_mmape_base = mmape_pages;
    ast_aste_base = aste_table;
    for (int i = 0; i < 4; i++) {
        aste_table[i].aote = &aote_entries[i];
    }
    status = 0x5555AAAA;
    PROC1_$CURRENT = 7;
    mock_set_ws_max_calls = 0;
    mock_purge_calls = 0;
    mock_get_pid_calls = 0;
}

static void call(uint8_t flags)
{
    flags_rec[1] = flags;
    OSINFO_$GET_MMAP(flags_rec, &counters, &info, ws_data, ws_list, &uid_out, &status);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* No flags: only the status is cleared (0x00E5C6B2). */
static void test_no_flags(void)
{
    call(0);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, mock_get_pid_calls);
}

/* Op 0: two interval words from +0x2a, the minimum from +0x2e. */
static void test_set_ws_interval(void)
{
    info.set_op = MMAP_SET_WS_INTERVAL;
    *(uint16_t *)((uint8_t *)&info + 0x2a) = 0x1111;
    info.asid = 0x2222;
    call(MMAP_FLAG_SET_PARAMS);
    ASSERT_EQ(0x1111, PMAP_$DATA.ws_interval);
    ASSERT_EQ(0x1111, PMAP_$DATA.max_ws_interval);
    ASSERT_EQ(0x2222, PMAP_$DATA.min_ws_interval);
}

/* Op 2: the WSL index is MMAP_$WS_OWNER[asid - 1]; the status is the callee's. */
static void test_set_ws_max(void)
{
    info.set_op = MMAP_SET_WS_MAX;
    info.asid = 3;
    info.set_value = 0x400;
    MMAP_$WS_OWNER[2] = 0x21;
    call(MMAP_FLAG_SET_PARAMS);
    ASSERT_EQ(1, mock_set_ws_max_calls);
    ASSERT_EQ(0x21, mock_set_ws_max_index);
    ASSERT_EQ(0x400, mock_set_ws_max_pages);
    ASSERT_EQ(0x00060009, status);
}

/* Op 3: ASID 0 and ASIDs above 0x40 mean the current process; the boolean
 * is pushed with `st -(SP)` and lands in the high byte of the word. */
static void test_purge_ws_asid_clamp(void)
{
    info.set_op = MMAP_PURGE_WS;
    info.asid = 0;
    call(MMAP_FLAG_SET_PARAMS);
    ASSERT_EQ(1, mock_purge_calls);
    ASSERT_EQ(7, mock_purge_index);
    ASSERT_EQ((int16_t)0xFF00, mock_purge_flags);

    info.asid = 0x41;
    call(MMAP_FLAG_SET_PARAMS);
    ASSERT_EQ(2, mock_purge_calls);
    ASSERT_EQ(7, mock_purge_index);

    info.asid = 0x40;
    call(MMAP_FLAG_SET_PARAMS);
    ASSERT_EQ(3, mock_purge_calls);
    ASSERT_EQ(0x40, mock_purge_index);
}

/* Op 4: MMAP_$WSL[MMAP_$WS_OWNER[asid-1]].ws_floor (0xE232B0 + n*0x24 + 0x20). */
static void test_set_ws_limit(void)
{
    info.set_op = MMAP_SET_WS_LIMIT;
    info.asid = 2;
    info.set_value = 0x77;
    MMAP_$WS_OWNER[1] = 5;
    call(MMAP_FLAG_SET_PARAMS);
    ASSERT_EQ(0x77, MMAP_$WSL[5].ws_floor);
    ASSERT_EQ(0, MMAP_$WSL[4].ws_floor);
}

/* An op of 5 or more does nothing (cmpi.w #0x5 / bcc). */
static void test_set_op_out_of_range(void)
{
    info.set_op = 5;
    info.asid = 1;
    call(MMAP_FLAG_SET_PARAMS);
    ASSERT_EQ(0, mock_purge_calls);
    ASSERT_EQ(0, mock_set_ws_max_calls);
}

/* FIND_PAGE with a bad ASID leaves at once: the GET_GLOBAL block that
 * would follow is skipped (0x00E5C7B8 -> 0x00E5C9F6). */
static void test_find_page_invalid_asid(void)
{
    info.asid = 0x46;
    MMAP_$REAL_PAGES = 0xDEAD;
    call(MMAP_FLAG_FIND_PAGE | MMAP_FLAG_GET_GLOBAL);
    ASSERT_EQ(status_$os_info_invalid_asid, status);
    ASSERT_EQ(0, info.real_pages);
}

/* A start above MMAP_$HPPN answers -1 and leaves. */
static void test_find_page_above_hppn(void)
{
    info.asid = 1;
    info.set_value = 12;
    MMAP_$LPPN = 2;
    MMAP_$HPPN = 10;
    call(MMAP_FLAG_FIND_PAGE | MMAP_FLAG_GET_PID);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0xFFFFFFFFu, info.set_value);
    ASSERT_EQ(0, mock_get_pid_calls);
}

/* The scan starts at max(start, LPPN); an in-use page of the ASID that is
 * not wired reports its owner's UID and the page number after it. */
static void test_find_page_found(void)
{
    info.asid = 3;
    info.set_value = 0;
    MMAP_$LPPN = 2;
    MMAP_$HPPN = 10;
    mmape_pages[4].flags1 = 0x80;
    mmape_pages[4].wsl_index = 3;
    mmape_pages[4].flags2 = 0x80;             /* wired: reported as such */
    mmape_pages[6].flags1 = 0x80;
    mmape_pages[6].wsl_index = 3;
    mmape_pages[6].segment = 2;
    aote_entries[1].uid.high = 0x11112222;
    aote_entries[1].uid.low = 0x33334444;

    /* page 4 matches first and is wired */
    call(MMAP_FLAG_FIND_PAGE);
    ASSERT_EQ(status_$os_info_page_wired, status);
    ASSERT_EQ(5, info.set_value);

    /* start at 5: page 6 is the hit, ASTE 2 -> aste_table[1] -> its AOTE */
    info.set_value = 5;
    call(MMAP_FLAG_FIND_PAGE);
    ASSERT_EQ(status_$os_info_page_found, status);
    ASSERT_EQ(7, info.set_value);
    ASSERT_EQ(0x11112222, uid_out.high);
    ASSERT_EQ(0x33334444, uid_out.low);

    /* start at 7: nothing up to HPPN, set_value ends one past it */
    info.set_value = 7;
    call(MMAP_FLAG_FIND_PAGE);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(11, info.set_value);
}

static void test_get_pid(void)
{
    call(MMAP_FLAG_GET_PID);
    ASSERT_EQ(1, mock_get_pid_calls);
    ASSERT_EQ((unsigned long)&uid_out, (unsigned long)mock_get_pid_uid);
    ASSERT_EQ(0x1234, info.pid);
}

static void test_get_global(void)
{
    MMAP_$REAL_PAGES = 1;
    MMAP_$PAGEABLE_PAGES = 2;
    MMAP_$REMOTE_PAGES = 3;
    MMAP_$WSL_HI_MARK = 4;
    for (int i = 0; i < 5; i++) {
        MMAP_$WSL[i].page_count = 0x100 + i;
    }
    PMAP_$DATA.ws_interval = 9;
    call(MMAP_FLAG_GET_GLOBAL);
    ASSERT_EQ(1, info.real_pages);
    ASSERT_EQ(2, info.pageable_lower_limit);
    ASSERT_EQ(3, info.remote_pages);
    ASSERT_EQ(4, info.wsl_hi_mark);
    ASSERT_EQ(0x100, info.ws_data[0]);
    ASSERT_EQ(0x104, info.ws_data[4]);
    ASSERT_EQ(9, info.ws_interval);
}

static void test_get_counters(void)
{
    PMAP_$DATA.pur_l_cnt = 1;
    PMAP_$DATA.pur_r_cnt = 2;
    ast_page_flt_cnt = 3;
    ast_ws_flt_cnt = 4;
    PMAP_$DATA.t_pur_scans = 5;
    MMAP_$ALLOC_CNT = 6;
    MMAP_$ALLOC_PAGES = 7;
    MMAP_$STEAL_CNT = 8;
    MMAP_$WS_OVERFLOW = 9;
    MMAP_$WS_SCAN_CNT = 10;
    ast_alloc_cnt = 11;
    ast_alloc_too_few_cnt = 12;
    MMAP_$RECLAIM_SHAR_CNT = 13;
    MMAP_$RECLAIM_PUR_CNT = 14;
    MMAP_$WS_REMOVE = 15;
    PMAP_$DATA.scan_fract = 16;
    call(MMAP_FLAG_GET_COUNTERS);
    ASSERT_EQ(1, counters.pur_l_cnt);
    ASSERT_EQ(2, counters.pur_r_cnt);
    ASSERT_EQ(3, counters.page_flt_cnt);
    ASSERT_EQ(4, counters.ws_flt_cnt);
    ASSERT_EQ(5, counters.t_pur_scans);
    ASSERT_EQ(6, counters.alloc_cnt);
    ASSERT_EQ(7, counters.alloc_pages);
    ASSERT_EQ(8, counters.steal_cnt);
    ASSERT_EQ(9, counters.ws_overflow);
    ASSERT_EQ(10, counters.ws_scan_cnt);
    ASSERT_EQ(11, counters.ast_alloc_cnt);
    ASSERT_EQ(12, counters.alloc_too_few);
    ASSERT_EQ(13, counters.reclaim_shar_cnt);
    ASSERT_EQ(14, counters.reclaim_pur_cnt);
    ASSERT_EQ(15, counters.ws_remove);
    ASSERT_EQ(16, counters.scan_fract);
}

/* GET_WS_LIST pairs MMAP_$WS_OWNER[i] with PROC1_$DATA.type[i + 1] - the word
 * at 0xE2612C is the map's PROC1_$DATA.type, entry 1 of the 0xE2612A array. */
static void test_get_ws_list(void)
{
    PROC1_$DATA.type[0] = 0xAAAA;            /* 0xE2612A: never reported */
    PROC1_$DATA.type[1] = 1;
    PROC1_$DATA.type[2] = 2;
    PROC1_$DATA.type[3] = 3;
    MMAP_$WS_OWNER[0] = 10;
    MMAP_$WS_OWNER[1] = 11;
    MMAP_$WS_OWNER[2] = 12;
    info.ws_list_count = 3;
    call(MMAP_FLAG_GET_WS_LIST);
    ASSERT_EQ(3, info.ws_list_count);
    ASSERT_EQ(10, ws_list[0]);
    ASSERT_EQ(1, ws_list[1]);
    ASSERT_EQ(11, ws_list[2]);
    ASSERT_EQ(2, ws_list[3]);
    ASSERT_EQ(12, ws_list[4]);
    ASSERT_EQ(3, ws_list[5]);
    ASSERT_EQ(0xEEEE, ws_list[6]);
}

/* The count is clamped to 0x40 (unsigned) and written back; 0 does nothing. */
static void test_get_ws_list_clamp(void)
{
    info.ws_list_count = 0x50;
    call(MMAP_FLAG_GET_WS_LIST);
    ASSERT_EQ(0x40, info.ws_list_count);
    ASSERT_EQ(0, ws_list[0x3F * 2]);
    ASSERT_EQ(0xEEEE, ws_list[0x40 * 2]);

    reset_state();
    info.ws_list_count = 0;
    call(MMAP_FLAG_GET_WS_LIST);
    ASSERT_EQ(0, info.ws_list_count);
    ASSERT_EQ(0xEEEE, ws_list[0]);
}

/* GET_WS_INFO: hi_mark - 5 + 1 entries from MMAP_$WSL[5], three longwords
 * each; nothing when the high mark is below 5. */
static void test_get_ws_info(void)
{
    MMAP_$WSL_HI_MARK = 6;
    MMAP_$WSL[5].page_count = 0x51;
    MMAP_$WSL[5].pri_timestamp = 0x52;
    MMAP_$WSL[5].ws_timestamp = 0x53;
    MMAP_$WSL[6].page_count = 0x61;
    MMAP_$WSL[6].pri_timestamp = 0x62;
    MMAP_$WSL[6].ws_timestamp = 0x63;
    call(MMAP_FLAG_GET_WS_INFO);
    ASSERT_EQ(0x51, ws_data[0]);
    ASSERT_EQ(0x52, ws_data[1]);
    ASSERT_EQ(0x53, ws_data[2]);
    ASSERT_EQ(0x61, ws_data[3]);
    ASSERT_EQ(0x62, ws_data[4]);
    ASSERT_EQ(0x63, ws_data[5]);
    ASSERT_EQ(0xEEEEEEEEu, ws_data[6]);

    reset_state();
    MMAP_$WSL_HI_MARK = 4;
    call(MMAP_FLAG_GET_WS_INFO);
    ASSERT_EQ(0xEEEEEEEEu, ws_data[0]);
}

int main(void)
{
    printf("OSINFO_$GET_MMAP tests\n");
    RUN_TEST(no_flags);
    RUN_TEST(set_ws_interval);
    RUN_TEST(set_ws_max);
    RUN_TEST(purge_ws_asid_clamp);
    RUN_TEST(set_ws_limit);
    RUN_TEST(set_op_out_of_range);
    RUN_TEST(find_page_invalid_asid);
    RUN_TEST(find_page_above_hppn);
    RUN_TEST(find_page_found);
    RUN_TEST(get_pid);
    RUN_TEST(get_global);
    RUN_TEST(get_counters);
    RUN_TEST(get_ws_list);
    RUN_TEST(get_ws_list_clamp);
    RUN_TEST(get_ws_info);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
