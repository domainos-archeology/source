/*
 * pmap/test/test_init_ws_scan.c - Unit tests for PMAP_$INIT_WS_SCAN
 * (0x00E145F0) and PMAP_$INIT_TIMERS (0x00E2F880)
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "pmap/pmap_internal.h"
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

time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];
time_queue_t TIME_$RTEQ;
time_queue_elem_t PMAP_$WS_TIMER_ELEMENTS[PMAP_WS_SLOTS];
time_queue_elem_t pmap_update_timer_elem;
time_queue_elem_t pmap_purifier_timer_elem;
uint32_t TIME_$CLOCKH;

static int set_ws_calls, removes, enters;
static uint16_t set_ws_pid, set_ws_val;
static time_queue_t *rm_queue, *en_queue[2];
static time_queue_elem_t *rm_elem, *en_elem[2];
static clock_t en_when[2];
static status_$t enter_status;
static jmp_buf crash_jmp;
static status_$t crash_status;

void MMAP_$SET_WS_INDEX(uint16_t pid, uint16_t *wsl_index)
{ set_ws_calls++; set_ws_pid = pid; set_ws_val = *wsl_index; }
void TIME_$Q_REMOVE_ELEM(time_queue_t *q, time_queue_elem_t *e, status_$t *st)
{ removes++; rm_queue = q; rm_elem = e; *st = 0; }
void TIME_$Q_ENTER_ELEM(time_queue_t *q, clock_t *when, time_queue_elem_t *e, status_$t *st)
{
    if (enters < 2) { en_queue[enters] = q; en_elem[enters] = e; en_when[enters] = *when; }
    enters++;
    *st = enter_status;
}
void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }
void PMAP_$WS_SCAN_CALLBACK(void *arg) { (void)arg; }
void PMAP_$T_PURIF_CALLBACK(void) {}
void PMAP_$UPDATE_CALLBACK(void) {}

#include "../init_ws_scan.c"
#include "../init_timers.c"

static void reset(void)
{
    memset(PMAP_$WS_TIMER_ELEMENTS, 0, sizeof PMAP_$WS_TIMER_ELEMENTS);
    set_ws_calls = removes = enters = 0;
    enter_status = 0;
}

TEST(ws_scan_uses_vtq_and_0x1c_stride)
{
    time_queue_elem_t *e;
    reset();
    PMAP_$INIT_WS_SCAN(7, 9);
    ASSERT_EQ(1, set_ws_calls);
    ASSERT_EQ(7, set_ws_pid);
    ASSERT_EQ(9, set_ws_val);
    ASSERT_EQ((unsigned long)&TIME_$VTQ[6], (unsigned long)rm_queue);
    e = (time_queue_elem_t *)((uint8_t *)PMAP_$WS_TIMER_ELEMENTS + 7 * 0x1C);
    ASSERT_EQ((unsigned long)e, (unsigned long)rm_elem);
    ASSERT_EQ(1, enters);
    ASSERT_EQ((unsigned long)&TIME_$VTQ[6], (unsigned long)en_queue[0]);
    ASSERT_EQ((unsigned long)e, (unsigned long)en_elem[0]);
    ASSERT_EQ(0, en_when[0].high);
    ASSERT_EQ(0, en_when[0].low);
    ASSERT_EQ(0x1A, e->flags);
    ASSERT_EQ(3, e->expire_high);
    ASSERT_EQ(0xD090, e->expire_low);
    ASSERT_EQ(3, e->interval_high);
    ASSERT_EQ(0xD090, e->interval_low);
    ASSERT_EQ(7, e->callback_arg);
    ASSERT_EQ(ARCH_PTR_TO_VA(PMAP_$WS_SCAN_CALLBACK), e->callback);
}

TEST(ws_scan_wired_pool_gets_no_timer)
{
    reset();
    PMAP_$INIT_WS_SCAN(3, 5);
    ASSERT_EQ(1, set_ws_calls);
    ASSERT_EQ(0, removes);
    ASSERT_EQ(0, enters);
}

TEST(timers_intervals)
{
    reset();
    TIME_$CLOCKH = 0x1000;
    PMAP_$INIT_TIMERS();
    ASSERT_EQ(2, enters);
    ASSERT_EQ((unsigned long)&pmap_purifier_timer_elem, (unsigned long)en_elem[0]);
    ASSERT_EQ((unsigned long)&pmap_update_timer_elem, (unsigned long)en_elem[1]);
    ASSERT_EQ((unsigned long)&TIME_$RTEQ, (unsigned long)en_queue[1]);
    ASSERT_EQ(0x1000, en_when[0].high);
    ASSERT_EQ(0, en_when[0].low);
    ASSERT_EQ(0x1A, pmap_purifier_timer_elem.flags);
    ASSERT_EQ(0x10E5, pmap_purifier_timer_elem.expire_high);
    ASSERT_EQ(0, pmap_purifier_timer_elem.expire_low);
    ASSERT_EQ(7, pmap_purifier_timer_elem.interval_high);
    ASSERT_EQ(0x270E, pmap_purifier_timer_elem.interval_low);
    ASSERT_EQ(0x16, pmap_update_timer_elem.flags);
    ASSERT_EQ(0x10E5, pmap_update_timer_elem.expire_high);
    ASSERT_EQ(0xE5, pmap_update_timer_elem.interval_high);
    ASSERT_EQ(0, pmap_update_timer_elem.interval_low);
}

TEST(timers_enter_failure_crashes)
{
    reset();
    enter_status = 0x40003;
    if (setjmp(crash_jmp) == 0) {
        PMAP_$INIT_TIMERS();
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(0x40003, crash_status);
    ASSERT_EQ(1, enters);
}

int main(void)
{
    printf("test_init_ws_scan:\n");
    RUN_TEST(ws_scan_uses_vtq_and_0x1c_stride);
    RUN_TEST(ws_scan_wired_pool_gets_no_timer);
    RUN_TEST(timers_intervals);
    RUN_TEST(timers_enter_failure_crashes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
