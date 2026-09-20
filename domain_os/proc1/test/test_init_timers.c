/*
 * Tests for PROC1_$INIT_LOADAV (0x00E14C94) and PROC1_$INIT_TS_TIMER
 * (0x00E14B12).  Includes the real .c files with TIME_$CLOCK, ADD48 and
 * TIME_$Q_ENTER_ELEM mocked.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"
#include "time/time.h"
#include "cal/cal.h"

int __host_intr_disable_count = 0;

static proc1_t pcb_table[PROC1_MAX_PROCESSES];
proc1_t *PCBS[PROC1_MAX_PROCESSES];
int32_t PROC1_$LOADAV[PROC1_LOADAV_COUNT];
time_queue_elem_t PROC1_$LOADAV_ELEM;
proc1_ts_slot_t PROC1_$TS_ELEM[PROC1_MAX_PROCESSES];
time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];
time_queue_t TIME_$RTEQ;

void PROC1_$LOADAV_CALLBACK(void) {}
void PROC1_$TS_END_CALLBACK(void *arg) { (void)arg; }

static clock_t clock_now;
void TIME_$CLOCK(clock_t *c) { *c = clock_now; }

static int n_add48;
void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t lo = (uint32_t)dst->low + src->low;
    n_add48++;
    dst->low = (uint16_t)lo;
    dst->high = dst->high + src->high + (lo >> 16);
}

static int n_enter;
static time_queue_t *q_queue; static clock_t q_when; static time_queue_elem_t *q_elem;
static status_$t *q_status;
void TIME_$Q_ENTER_ELEM(time_queue_t *queue, clock_t *when, time_queue_elem_t *elem, status_$t *status)
{
    n_enter++;
    q_queue = queue; q_when = *when; q_elem = elem; q_status = status;
}

#include "../init_loadav.c"
#include "../init_ts_timer.c"

static int tests_run, tests_failed;

#define ASSERT_EQ(a, b) do {                                                  \
    unsigned long long _a = (unsigned long long)(a);                          \
    unsigned long long _b = (unsigned long long)(b);                          \
    if (_a != _b) {                                                           \
        printf("  FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", __FILE__,       \
               __LINE__, #a, #b, _a, _b);                                     \
        tests_failed++;                                                       \
    }                                                                         \
} while (0)

#define RUN_TEST(fn) do { tests_run++; reset(); fn(); } while (0)

static void reset(void)
{
    unsigned i;
    memset(pcb_table, 0, sizeof(pcb_table));
    for (i = 0; i < PROC1_MAX_PROCESSES; i++) PCBS[i] = &pcb_table[i];
    memset(&PROC1_$LOADAV_ELEM, 0xEE, sizeof(PROC1_$LOADAV_ELEM));
    memset(PROC1_$TS_ELEM, 0xEE, sizeof(PROC1_$TS_ELEM));
    PROC1_$LOADAV[0] = PROC1_$LOADAV[1] = PROC1_$LOADAV[2] = 0x11111111;
    clock_now.high = 0x00001000; clock_now.low = 0xF000;
    n_add48 = n_enter = 0;
}

/* 0x00E14CA0..0x00E14D0E, field by field */
static void test_init_loadav(void)
{
    PROC1_$INIT_LOADAV();

    ASSERT_EQ(PROC1_$LOADAV[0], 0);
    ASSERT_EQ(PROC1_$LOADAV[1], 0);
    ASSERT_EQ(PROC1_$LOADAV[2], 0);
    ASSERT_EQ(PROC1_$LOADAV_ELEM.flags, TIME_QELEM_REPEAT);
    ASSERT_EQ(PROC1_$LOADAV_ELEM.callback, (uint32_t)(uintptr_t)PROC1_$LOADAV_CALLBACK);
    ASSERT_EQ(PROC1_$LOADAV_ELEM.callback_arg, 0);
    ASSERT_EQ(PROC1_$LOADAV_ELEM.interval_high, 0x13);
    ASSERT_EQ(PROC1_$LOADAV_ELEM.interval_low, 0x12D0);
    /* expire = interval + now, carrying out of the low word */
    ASSERT_EQ(PROC1_$LOADAV_ELEM.expire_high, 0x00001014);
    ASSERT_EQ(PROC1_$LOADAV_ELEM.expire_low, 0x02D0);
    /* next is untouched (0xEEEEEEEE) */
    ASSERT_EQ(PROC1_$LOADAV_ELEM.next, 0xEEEEEEEE);
    ASSERT_EQ(n_add48, 1);
    ASSERT_EQ(n_enter, 1);
    ASSERT_EQ((uintptr_t)q_queue, (uintptr_t)&TIME_$RTEQ);
    ASSERT_EQ(q_when.high, 0x00001000);
    ASSERT_EQ(q_when.low, 0xF000);
    ASSERT_EQ((uintptr_t)q_elem, (uintptr_t)&PROC1_$LOADAV_ELEM);
}

/* 0x00E14B34..0x00E14BAA for pid 5 */
static void test_init_ts_timer(void)
{
    time_queue_elem_t *e = &PROC1_$TS_ELEM[5].elem;

    pcb_table[5].cpu_total = 0x00000022;
    pcb_table[5].cpu_usage = 0x0003;
    PROC1_$INIT_TS_TIMER(5);

    ASSERT_EQ(e->flags, 0);
    /* expire = {0x22, 0x0003} + {0, 0xFFFF} */
    ASSERT_EQ(e->expire_high, 0x00000023);
    ASSERT_EQ(e->expire_low, 0x0002);
    ASSERT_EQ(e->callback, (uint32_t)(uintptr_t)PROC1_$TS_END_CALLBACK);
    ASSERT_EQ(e->callback_arg, 5);
    ASSERT_EQ(e->next, 0xEEEEEEEE);
    ASSERT_EQ(e->interval_high, 0xEEEEEEEE);
    ASSERT_EQ(n_enter, 1);
    /* 0x00E14BA6: element pid-1 of TIME_$VTQ */
    ASSERT_EQ((uintptr_t)q_queue, (uintptr_t)&TIME_$VTQ[4]);
    /* the `when' handed to the queue is the PCB time, not the expiry */
    ASSERT_EQ(q_when.high, 0x00000022);
    ASSERT_EQ(q_when.low, 0x0003);
    ASSERT_EQ((uintptr_t)q_elem, (uintptr_t)e);
    /* neighbouring slots untouched */
    ASSERT_EQ(PROC1_$TS_ELEM[4].elem.flags, 0xEEEE);
    ASSERT_EQ(PROC1_$TS_ELEM[6].elem.flags, 0xEEEE);
}

/* pid 1 maps to TIME_$VTQ[0]; pid 64 to TIME_$VTQ[63] */
static void test_init_ts_timer_queue_index(void)
{
    PROC1_$INIT_TS_TIMER(1);
    ASSERT_EQ((uintptr_t)q_queue, (uintptr_t)&TIME_$VTQ[0]);
    PROC1_$INIT_TS_TIMER(64);
    ASSERT_EQ((uintptr_t)q_queue, (uintptr_t)&TIME_$VTQ[63]);
    ASSERT_EQ(PROC1_$TS_ELEM[64].elem.callback_arg, 64);
}

int main(void)
{
    RUN_TEST(test_init_loadav);
    RUN_TEST(test_init_ts_timer);
    RUN_TEST(test_init_ts_timer_queue_index);
    printf("test_init_timers: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
