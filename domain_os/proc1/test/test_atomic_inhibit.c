/*
 * Tests for PROC1_$BEGIN_ATOMIC_OP (0x00E209E6), PROC1_$END_ATOMIC_OP
 * (0x00E209FA), PROC1_$INHIBIT_BEGIN (0x00E20EFC), PROC1_$INHIBIT_CHECK
 * (0x00E20EF0) and PROC1_$INIT (0x00E2F958).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"
#include "pmap/pmap.h"

int __host_intr_disable_count = 0;

static proc1_t pcb_table[PROC1_MAX_PROCESSES];
proc1_t *PCBS[PROC1_MAX_PROCESSES];
proc1_t *PROC1_$CURRENT_PCB;
uint16_t PROC1_$CURRENT;
uint16_t PROC1_$ATOMIC_OP_DEPTH;
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

static int n_dispatch_int, ipl_at_dispatch_int, depth_at_dispatch_int;
void PROC1_$DISPATCH_INT(void)
{
    n_dispatch_int++;
    ipl_at_dispatch_int = __host_intr_disable_count;
    depth_at_dispatch_int = PROC1_$ATOMIC_OP_DEPTH;
}

static int n_set_type; static uint16_t st_pid, st_type;
void PROC1_$SET_TYPE(uint16_t pid, uint16_t type) { n_set_type++; st_pid = pid; st_type = type; }

static int n_reorder; static proc1_t *reorder_pcb;
void PROC1_$REORDER_READY(proc1_t *pcb) { n_reorder++; reorder_pcb = pcb; }

static int n_add_ready; static proc1_t *add_ready_pcb; static int ipl_at_add_ready;
void PROC1_$ADD_READY(proc1_t *pcb) { n_add_ready++; add_ready_pcb = pcb; ipl_at_add_ready = __host_intr_disable_count; }

static int n_ts; static uint16_t ts_pids[4];
void PROC1_$INIT_TS_TIMER(uint16_t pid) { if (n_ts < 4) ts_pids[n_ts] = pid; n_ts++; }

static int n_dispatch, ipl_at_dispatch, ts_at_dispatch;
void PROC1_$DISPATCH(void) { n_dispatch++; ipl_at_dispatch = __host_intr_disable_count; ts_at_dispatch = n_ts; __host_intr_disable_count = 0; }

static int n_ws; static uint16_t ws_idx[4]; static int16_t ws_param[4]; static int dispatch_at_ws;
void PMAP_$INIT_WS_SCAN(uint16_t index, int16_t param)
{
    if (n_ws < 4) { ws_idx[n_ws] = index; ws_param[n_ws] = param; }
    n_ws++;
    dispatch_at_ws = n_dispatch;
}

#include "../begin_atomic_op.c"
#include "../end_atomic_op.c"
#include "../inhibit_begin.c"
#include "../inhibit_check.c"
#include "../init.c"

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
    for (i = 0; i < PROC1_MAX_PROCESSES; i++) {
        PCBS[i] = &pcb_table[i];
        pcb_table[i].mypid = (uint16_t)i;
    }
    PROC1_$CURRENT_PCB = &pcb_table[3];
    PROC1_$CURRENT = 0;
    PROC1_$ATOMIC_OP_DEPTH = 0;
    __host_intr_disable_count = 0;
    n_dispatch_int = n_set_type = n_reorder = n_add_ready = n_ts = n_dispatch = n_ws = 0;
}

/* 0x00E209E6 / 0x00E209FA: plain increment and decrement */
static void test_atomic_pair(void)
{
    PROC1_$BEGIN_ATOMIC_OP();
    ASSERT_EQ(PROC1_$ATOMIC_OP_DEPTH, 1);
    PROC1_$BEGIN_ATOMIC_OP();
    ASSERT_EQ(PROC1_$ATOMIC_OP_DEPTH, 2);
    PROC1_$END_ATOMIC_OP();
    ASSERT_EQ(PROC1_$ATOMIC_OP_DEPTH, 1);
    PROC1_$END_ATOMIC_OP();
    ASSERT_EQ(PROC1_$ATOMIC_OP_DEPTH, 0);
    ASSERT_EQ(n_dispatch_int, 0);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

/* 0x00E20A00: 0 - 1 = 0xFFFF sets no V flag, so no dispatch (bvc taken) */
static void test_end_atomic_underflow_is_not_overflow(void)
{
    PROC1_$END_ATOMIC_OP();
    ASSERT_EQ(PROC1_$ATOMIC_OP_DEPTH, 0xFFFF);
    ASSERT_EQ(n_dispatch_int, 0);
}

/* 0x00E20A02..0x00E20A12: 0x8000 - 1 overflows: depth forced 0, dispatch at IPL 7, exit IPL 0 */
static void test_end_atomic_overflow_arm(void)
{
    PROC1_$ATOMIC_OP_DEPTH = 0x8000;
    __host_intr_disable_count = 0;
    PROC1_$END_ATOMIC_OP();
    ASSERT_EQ(n_dispatch_int, 1);
    ASSERT_EQ(depth_at_dispatch_int, 0);
    ASSERT_EQ(ipl_at_dispatch_int, 1);
    ASSERT_EQ(PROC1_$ATOMIC_OP_DEPTH, 0);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

/* 0x00E20F00 / 0x00E20F04 */
static void test_inhibit_begin(void)
{
    proc1_t *p = PROC1_$CURRENT_PCB;
    p->nesting_depth = 4;
    p->resource_locks_held = 0x00000100;
    PROC1_$INHIBIT_BEGIN();
    ASSERT_EQ(p->nesting_depth, 5);
    ASSERT_EQ(p->resource_locks_held, 0x00000101);
    PROC1_$INHIBIT_BEGIN();
    ASSERT_EQ(p->nesting_depth, 6);
    ASSERT_EQ(p->resource_locks_held, 0x00000101);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

/* 0x00E20EF4 / 0x00E20EF8: sne */
static void test_inhibit_check(void)
{
    proc1_t p;
    memset(&p, 0, sizeof(p));
    ASSERT_EQ((uint8_t)PROC1_$INHIBIT_CHECK(&p), 0x00);
    p.nesting_depth = 1;
    ASSERT_EQ((uint8_t)PROC1_$INHIBIT_CHECK(&p), 0xFF);
    p.nesting_depth = 0x8000;
    ASSERT_EQ(PROC1_$INHIBIT_CHECK(&p) < 0, 1);
}

/* 0x00E2F958: the whole boot sequence with PCBS[1] not yet bound */
static void test_init_fresh_pcb(void)
{
    proc1_t *p = &pcb_table[1];
    p->pri_min = 0x55;
    p->pri_max = PROC1_FLAG_SUSPENDED;      /* & 0xB != 8 */
    p->resource_locks_held = 0xFFFFFFFF;

    PROC1_$INIT();

    ASSERT_EQ(PROC1_$DATA.stack_low_water, 0x00D00000);
    ASSERT_EQ(PROC1_$DATA.stack_high_water, 0x00D50000);
    ASSERT_EQ((uintptr_t)PROC1_$DATA.stack_free_list, 0);
    ASSERT_EQ(PROC1_$DATA.os_stack_base[1], 0x00EB2000);
    ASSERT_EQ(PROC1_$DATA.os_stack_base[2], 0);
    ASSERT_EQ(n_set_type, 1);
    ASSERT_EQ(st_pid, 2);
    ASSERT_EQ(st_type, 3);
    /* 0x00E2F98C..0x00E2F9A0: PCBS[1], not PCBS[2] */
    ASSERT_EQ(p->state, 0x10);
    ASSERT_EQ(p->inh_count, 1);
    ASSERT_EQ(p->sw_bsr, 0x10);
    ASSERT_EQ(p->resource_locks_held, 0);
    ASSERT_EQ(pcb_table[2].state, 0);
    /* 0x00E2F9BE: word store clears pri_min too */
    ASSERT_EQ(p->pri_min, 0);
    ASSERT_EQ(p->pri_max, PROC1_FLAG_BOUND);
    ASSERT_EQ(n_add_ready, 1);
    ASSERT_EQ((uintptr_t)add_ready_pcb, (uintptr_t)p);
    ASSERT_EQ(ipl_at_add_ready, 1);
    ASSERT_EQ(n_reorder, 0);
    ASSERT_EQ((uintptr_t)PROC1_$CURRENT_PCB, (uintptr_t)p);
    ASSERT_EQ(PROC1_$CURRENT, 1);
    ASSERT_EQ(n_ts, 2);
    ASSERT_EQ(ts_pids[0], 2);
    ASSERT_EQ(ts_pids[1], 1);
    ASSERT_EQ(n_dispatch, 1);
    ASSERT_EQ(ipl_at_dispatch, 1);
    ASSERT_EQ(ts_at_dispatch, 2);
    ASSERT_EQ(n_ws, 2);
    ASSERT_EQ(dispatch_at_ws, 1);
    ASSERT_EQ(ws_idx[0], 2); ASSERT_EQ(ws_param[0], 5);
    ASSERT_EQ(ws_idx[1], 1); ASSERT_EQ(ws_param[1], 7);
}

/* 0x00E2F9A8..0x00E2F9B6: already BOUND and neither SUSPENDED nor WAITING -> reorder */
static void test_init_reorders_bound_pcb(void)
{
    proc1_t *p = &pcb_table[1];
    p->pri_min = 0x55;
    p->pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_DEFER_SUSP;   /* bit 2 ignored */

    PROC1_$INIT();

    ASSERT_EQ(n_reorder, 1);
    ASSERT_EQ((uintptr_t)reorder_pcb, (uintptr_t)p);
    ASSERT_EQ(n_add_ready, 0);
    ASSERT_EQ(p->pri_min, 0x55);
    ASSERT_EQ(p->pri_max, PROC1_FLAG_BOUND | PROC1_FLAG_DEFER_SUSP);
}

int main(void)
{
    RUN_TEST(test_atomic_pair);
    RUN_TEST(test_end_atomic_underflow_is_not_overflow);
    RUN_TEST(test_end_atomic_overflow_arm);
    RUN_TEST(test_inhibit_begin);
    RUN_TEST(test_inhibit_check);
    RUN_TEST(test_init_fresh_pcb);
    RUN_TEST(test_init_reorders_bound_pcb);
    printf("test_atomic_inhibit: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
