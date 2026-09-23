/*
 * netbuf/test/test_get_buffers.c - Unit tests for NETBUF_$GET_DAT_COND /
 * NETBUF_$GET_DAT (0x00E0EF28 / 0x00E0EFA4) and NETBUF_$GET_HDR_COND /
 * NETBUF_$GET_HDR (0x00E0ED6C / 0x00E0EDD6).
 *
 * The real netbuf/get_dat.c and netbuf/get_hdr.c are compiled against scripted
 * versions of the spin lock, TIME_$WAIT, WP_$CALLOC, NETBUF_$GETVA and
 * CRASH_SYSTEM, a real MMAPE array for the data-page free list and a real
 * 1KB-per-buffer arena for the header buffers.
 */

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    unsigned long _a = (unsigned long)(a); \
    unsigned long _b = (unsigned long)(b); \
    if (_a != _b) { \
        printf("FAILED\n    %s = 0x%lx (%lu), %s = 0x%lx (%lu) at line %d\n", \
               #a, _a, _a, #b, _b, _b, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Globals and mocks the code under test links against
 * ========================================================================== */

#include "netbuf/netbuf_internal.h"
#include "proc1/proc1.h"

static netbuf_globals_t globals_storage;
netbuf_globals_t *netbuf_globals = &globals_storage;
uint32_t netbuf_va_base = 0xD64C00;

#define TEST_MMAPE_ENTRIES 512
static mmape_t mmape_storage[TEST_MMAPE_ENTRIES];
mmape_t *mmap_mmape_base = mmape_storage;

uint16_t NETBUF_$DELAY_TYPE = 0;

static proc1_t current_pcb;
proc1_t *PROC1_$CURRENT_PCB = &current_pcb;
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];

#define TEST_ARENA_PAGES 8
static uint8_t arena[(TEST_ARENA_PAGES + 1) * NETBUF_HDR_SIZE];

static int              lock_calls, unlock_calls, lock_depth;
static ml_$spin_token_t next_token, unlock_token_seen;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    (void)lockp;
    lock_calls++;
    lock_depth++;
    return next_token;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    (void)lockp;
    unlock_calls++;
    lock_depth--;
    unlock_token_seen = token;
}

/* Scripted TIME_$WAIT: records the delay it was handed, runs a hook (the
 * "somebody returned a buffer" event) and reports time_wait_status. */
static int       time_wait_calls;
static uint16_t  time_wait_type_seen;
static clock_t  *time_wait_delay_seen;
static status_$t time_wait_status;
static void    (*time_wait_hook)(void);

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    time_wait_calls++;
    time_wait_type_seen = *delay_type;
    time_wait_delay_seen = delay;
    if (time_wait_hook != NULL) {
        time_wait_hook();
    }
    *status = time_wait_status;
}

static int       calloc_calls;
static uint32_t  calloc_ppn;
static status_$t calloc_status;

void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    calloc_calls++;
    *ppn_out = calloc_ppn;
    *status = calloc_status;
}

static int       getva_calls;
static uint32_t  getva_pa_seen;
static uint32_t  getva_va;
static status_$t getva_status;

void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status)
{
    getva_calls++;
    getva_pa_seen = ppn_shifted;
    *va_out = getva_va;
    *status = getva_status;
}

static int       crash_calls;
static jmp_buf   crash_jmp;
static status_$t crash_status_seen;

void CRASH_SYSTEM(const status_$t *status)
{
    crash_calls++;
    crash_status_seen = *status;
    longjmp(crash_jmp, 1);
}

#include "../get_dat.c"
#include "../get_hdr.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void reset_state(void)
{
    memset(&globals_storage, 0, sizeof(globals_storage));
    memset(mmape_storage, 0, sizeof(mmape_storage));
    memset(arena, 0xA5, sizeof(arena));
    memset(PROC1_$TYPE, 0, sizeof(PROC1_$TYPE));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    current_pcb.mypid = 9;
    lock_calls = unlock_calls = lock_depth = 0;
    next_token = 0x1234;
    unlock_token_seen = 0;
    time_wait_calls = 0;
    time_wait_status = status_$ok;
    time_wait_hook = NULL;
    calloc_calls = 0;
    calloc_ppn = 0x321;
    calloc_status = status_$ok;
    getva_calls = 0;
    getva_va = 3 * NETBUF_HDR_SIZE;
    getva_status = status_$ok;
    crash_calls = 0;
}

/* Thread data pages ppn[0..n-1] onto the data free list, head first. */
static void push_dat_pages(const uint32_t *ppn, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        mmape_storage[ppn[i]].prev_vpn = (i + 1 < n) ? (uint16_t)ppn[i + 1] : 0;
    }
    globals_storage.dat_top = ppn[0];
    globals_storage.dat_cnt = (uint32_t)n;
}

/* Thread header buffers (arena slots) onto the header free list. */
static void push_hdr_buffers(const int *slot, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        uint32_t va = (uint32_t)(slot[i] * NETBUF_HDR_SIZE);
        NETBUF_HDR_NEXT(va) = (i + 1 < n) ? (uint32_t)(slot[i + 1] * NETBUF_HDR_SIZE) : 0;
        NETBUF_HDR_PHYS(va) = 0x00100000u + (uint32_t)slot[i] * 0x400;
    }
    globals_storage.hdr_top = (uint32_t)(slot[0] * NETBUF_HDR_SIZE);
}

#define CALL_GUARDED(call) do { if (setjmp(crash_jmp) == 0) { call; } } while (0)

/* ==========================================================================
 * NETBUF_$GET_DAT_COND (0x00E0EF28)
 * ========================================================================== */

TEST(get_dat_cond_pops_the_free_list)
{
    static const uint32_t pages[3] = { 0x40, 0x17, 0x1FF };
    uint32_t addr = 0xDEADBEEF;
    reset_state();
    push_dat_pages(pages, 3);

    ASSERT_EQ((uint8_t)NETBUF_$GET_DAT_COND(&addr), 0xFF);
    ASSERT_EQ(addr, 0x40u << 10);
    ASSERT_EQ(globals_storage.dat_top, 0x17);
    ASSERT_EQ(globals_storage.dat_cnt, 2);
    ASSERT_EQ(lock_calls, 1);
    ASSERT_EQ(unlock_calls, 1);
    ASSERT_EQ(unlock_token_seen, 0x1234);

    ASSERT_EQ((uint8_t)NETBUF_$GET_DAT_COND(&addr), 0xFF);
    ASSERT_EQ(addr, 0x17u << 10);
    ASSERT_EQ((uint8_t)NETBUF_$GET_DAT_COND(&addr), 0xFF);
    ASSERT_EQ(addr, 0x1FFu << 10);
    ASSERT_EQ(globals_storage.dat_top, 0);
    ASSERT_EQ(globals_storage.dat_cnt, 0);
}

TEST(get_dat_cond_empty_pool_reports_false_and_zero)
{
    uint32_t addr = 0xDEADBEEF;
    reset_state();
    globals_storage.dat_top = 0x55;     /* stale head, count says empty */
    globals_storage.dat_cnt = 0;

    ASSERT_EQ((uint8_t)NETBUF_$GET_DAT_COND(&addr), 0);
    ASSERT_EQ(addr, 0);
    ASSERT_EQ(globals_storage.dat_top, 0x55);
    ASSERT_EQ(lock_calls, 1);
    ASSERT_EQ(unlock_calls, 1);
    ASSERT_EQ(lock_depth, 0);
}

/* ==========================================================================
 * NETBUF_$GET_DAT (0x00E0EFA4)
 * ========================================================================== */

TEST(get_dat_takes_from_the_pool_first)
{
    static const uint32_t pages[1] = { 0x80 };
    uint32_t addr = 0;
    reset_state();
    push_dat_pages(pages, 1);
    CALL_GUARDED(NETBUF_$GET_DAT(&addr));
    ASSERT_EQ(addr, 0x80u << 10);
    ASSERT_EQ(calloc_calls, 0);
    ASSERT_EQ(time_wait_calls, 0);
    ASSERT_EQ(globals_storage.dat_allocs, 0);
}

TEST(get_dat_non_network_process_wires_a_page)
{
    uint32_t addr = 0;
    reset_state();
    PROC1_$TYPE[9] = 3;
    calloc_ppn = 0x123;
    CALL_GUARDED(NETBUF_$GET_DAT(&addr));
    ASSERT_EQ(calloc_calls, 1);
    ASSERT_EQ(time_wait_calls, 0);
    ASSERT_EQ(addr, 0x123u << 10);
    ASSERT_EQ(globals_storage.dat_allocs, 1);
    ASSERT_EQ(crash_calls, 0);
}

static void hook_return_dat_page(void)
{
    static const uint32_t pages[1] = { 0x77 };
    push_dat_pages(pages, 1);
}

TEST(get_dat_network_process_waits_then_retries)
{
    uint32_t addr = 0;
    reset_state();
    PROC1_$TYPE[9] = NETBUF_NETWORK_PROC_TYPE;
    globals_storage.delay_time.high = 0;
    globals_storage.delay_time.low = 250;
    time_wait_hook = hook_return_dat_page;
    CALL_GUARDED(NETBUF_$GET_DAT(&addr));
    ASSERT_EQ(time_wait_calls, 1);
    ASSERT_EQ(time_wait_type_seen, 0);
    ASSERT_EQ(time_wait_delay_seen == &globals_storage.delay_time, 1);
    ASSERT_EQ(calloc_calls, 0);
    ASSERT_EQ(addr, 0x77u << 10);
    ASSERT_EQ(globals_storage.dat_delays, 1);
    ASSERT_EQ(globals_storage.dat_allocs, 0);
}

TEST(get_dat_crashes_on_a_bad_wait_or_calloc_status)
{
    uint32_t addr = 0;
    reset_state();
    PROC1_$TYPE[9] = NETBUF_NETWORK_PROC_TYPE;
    time_wait_status = 0x00040005;
    CALL_GUARDED(NETBUF_$GET_DAT(&addr));
    ASSERT_EQ(crash_calls, 1);
    ASSERT_EQ(crash_status_seen, 0x00040005);
    ASSERT_EQ(globals_storage.dat_delays, 0);

    reset_state();
    PROC1_$TYPE[9] = 1;
    calloc_status = 0x00110001;
    CALL_GUARDED(NETBUF_$GET_DAT(&addr));
    ASSERT_EQ(crash_calls, 1);
    ASSERT_EQ(crash_status_seen, 0x00110001);
    ASSERT_EQ(globals_storage.dat_allocs, 0);
}

/* ==========================================================================
 * NETBUF_$GET_HDR_COND (0x00E0ED6C)
 * ========================================================================== */

TEST(get_hdr_cond_pops_the_free_list)
{
    static const int slots[2] = { 2, 5 };
    uint32_t phys = 0, va = 0;
    reset_state();
    push_hdr_buffers(slots, 2);

    ASSERT_EQ((uint8_t)NETBUF_$GET_HDR_COND(&phys, &va), 0xFF);
    ASSERT_EQ(va, 2 * NETBUF_HDR_SIZE);
    ASSERT_EQ(phys, 0x00100000u + 2 * 0x400);
    ASSERT_EQ(globals_storage.hdr_top, 5 * NETBUF_HDR_SIZE);
    ASSERT_EQ(lock_calls, 1);
    ASSERT_EQ(unlock_calls, 1);

    ASSERT_EQ((uint8_t)NETBUF_$GET_HDR_COND(&phys, &va), 0xFF);
    ASSERT_EQ(va, 5 * NETBUF_HDR_SIZE);
    ASSERT_EQ(globals_storage.hdr_top, 0);
}

TEST(get_hdr_cond_empty_pool_leaves_phys_alone)
{
    uint32_t phys = 0xABCD, va = 0x1111;
    reset_state();
    ASSERT_EQ((uint8_t)NETBUF_$GET_HDR_COND(&phys, &va), 0);
    ASSERT_EQ(va, 0);
    ASSERT_EQ(phys, 0xABCD);
    ASSERT_EQ(lock_depth, 0);
}

/* ==========================================================================
 * NETBUF_$GET_HDR (0x00E0EDD6)
 * ========================================================================== */

TEST(get_hdr_non_network_process_wires_maps_and_initialises)
{
    uint32_t phys = 0, va = 0;
    uint8_t *buf;
    int k;
    reset_state();
    PROC1_$TYPE[9] = 2;
    calloc_ppn = 0x210;
    getva_va = 4 * NETBUF_HDR_SIZE;
    CALL_GUARDED(NETBUF_$GET_HDR(&phys, &va));
    ASSERT_EQ(calloc_calls, 1);
    ASSERT_EQ(getva_calls, 1);
    ASSERT_EQ(getva_pa_seen, 0x210u << 10);
    ASSERT_EQ(phys, 0x210u << 10);
    ASSERT_EQ(va, 4 * NETBUF_HDR_SIZE);
    ASSERT_EQ(globals_storage.hdr_allocs, 1);

    buf = arena + 4 * NETBUF_HDR_SIZE;
    /* 0x3EC..0x3FB cleared, 0x3E8..0x3EB untouched, 0x3FC = phys */
    for (k = 0x3EC; k < 0x3FC; k++) {
        ASSERT_EQ(buf[k], 0);
    }
    ASSERT_EQ(buf[0x3E8], 0xA5);
    ASSERT_EQ(buf[0x3EB], 0xA5);
    ASSERT_EQ(NETBUF_HDR_PHYS(va), 0x210u << 10);
    ASSERT_EQ(crash_calls, 0);
}

static void hook_return_hdr_buffer(void)
{
    static const int slots[1] = { 6 };
    push_hdr_buffers(slots, 1);
}

TEST(get_hdr_network_process_waits_then_retries)
{
    uint32_t phys = 0, va = 0;
    reset_state();
    PROC1_$TYPE[9] = NETBUF_NETWORK_PROC_TYPE;
    time_wait_hook = hook_return_hdr_buffer;
    CALL_GUARDED(NETBUF_$GET_HDR(&phys, &va));
    ASSERT_EQ(time_wait_calls, 1);
    ASSERT_EQ(va, 6 * NETBUF_HDR_SIZE);
    ASSERT_EQ(phys, 0x00100000u + 6 * 0x400);
    ASSERT_EQ(globals_storage.hdr_delays, 1);
    ASSERT_EQ(calloc_calls, 0);
}

TEST(get_hdr_calloc_failure_skips_getva_and_crashes)
{
    uint32_t phys = 0, va = 0;
    reset_state();
    PROC1_$TYPE[9] = 1;
    calloc_ppn = 0x33;
    calloc_status = 0x00110001;
    CALL_GUARDED(NETBUF_$GET_HDR(&phys, &va));
    ASSERT_EQ(phys, 0x33u << 10);     /* stored before the status test */
    ASSERT_EQ(getva_calls, 0);
    ASSERT_EQ(crash_calls, 1);
    ASSERT_EQ(crash_status_seen, 0x00110001);
    ASSERT_EQ(globals_storage.hdr_allocs, 0);

    reset_state();
    PROC1_$TYPE[9] = 1;
    getva_status = 0x00110002;
    CALL_GUARDED(NETBUF_$GET_HDR(&phys, &va));
    ASSERT_EQ(getva_calls, 1);
    ASSERT_EQ(crash_calls, 1);
    ASSERT_EQ(crash_status_seen, 0x00110002);
}

/* ==========================================================================
 * main
 * ========================================================================== */

int main(void)
{
    printf("NETBUF_$GET_DAT / NETBUF_$GET_HDR tests\n");

    RUN_TEST(get_dat_cond_pops_the_free_list);
    RUN_TEST(get_dat_cond_empty_pool_reports_false_and_zero);
    RUN_TEST(get_dat_takes_from_the_pool_first);
    RUN_TEST(get_dat_non_network_process_wires_a_page);
    RUN_TEST(get_dat_network_process_waits_then_retries);
    RUN_TEST(get_dat_crashes_on_a_bad_wait_or_calloc_status);
    RUN_TEST(get_hdr_cond_pops_the_free_list);
    RUN_TEST(get_hdr_cond_empty_pool_leaves_phys_alone);
    RUN_TEST(get_hdr_non_network_process_wires_maps_and_initialises);
    RUN_TEST(get_hdr_network_process_waits_then_retries);
    RUN_TEST(get_hdr_calloc_failure_skips_getva_and_crashes);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
