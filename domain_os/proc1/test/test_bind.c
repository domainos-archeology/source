/*
 * Tests for PROC1_$BIND (0x00E14D1C).
 *
 * Includes the real proc1/bind.c and drives it through mocked ML_$LOCK /
 * ML_$UNLOCK / PMAP_$INIT_WS_SCAN / INIT_STACK / PROC1_$INIT_TS_TIMER.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"
#include "ml/ml.h"
#include "pmap/pmap.h"

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Module cells                                                        */
/* ------------------------------------------------------------------ */

static proc1_t pcb_table[PROC1_MAX_PROCESSES];
proc1_t *PCBS[PROC1_MAX_PROCESSES];
void *OS_STACK_BASE[PROC1_MAX_PROCESSES];
uint32_t PROC_STATS_BASE[PROC1_MAX_PROCESSES * 4];

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static int n_lock, n_unlock;
static int16_t last_lock_id, last_unlock_id;

void ML_$LOCK(int16_t id)   { n_lock++;   last_lock_id = id; }
void ML_$UNLOCK(int16_t id) { n_unlock++; last_unlock_id = id; }

static int n_ws_scan;
static uint16_t ws_scan_pid;
static int16_t ws_scan_param;
static int unlocks_at_ws_scan;

void PMAP_$INIT_WS_SCAN(uint16_t index, int16_t param)
{
    n_ws_scan++;
    ws_scan_pid = index;
    ws_scan_param = param;
    unlocks_at_ws_scan = n_unlock;
}

static int n_init_stack;
static proc1_t *init_stack_pcb;
static void *init_stack_entry;
static void *init_stack_sp;
static int unlocks_at_init_stack;
static uint8_t pri_max_at_init_stack;

void INIT_STACK(proc1_t *pcb, void **entry_ptr, void **sp_ptr)
{
    n_init_stack++;
    init_stack_pcb = pcb;
    init_stack_entry = *entry_ptr;
    init_stack_sp = *sp_ptr;
    unlocks_at_init_stack = n_unlock;
    pri_max_at_init_stack = pcb->pri_max;
}

static int n_ts_timer;
static uint16_t ts_timer_pid;
static int init_stacks_at_ts_timer;

void PROC1_$INIT_TS_TIMER(uint16_t pid)
{
    n_ts_timer++;
    ts_timer_pid = pid;
    init_stacks_at_ts_timer = n_init_stack;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "../bind.c"

/* ------------------------------------------------------------------ */

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
    memset(pcb_table, 0xA5, sizeof(pcb_table));
    for (i = 0; i < PROC1_MAX_PROCESSES; i++) {
        PCBS[i] = &pcb_table[i];
        pcb_table[i].pri_max = PROC1_FLAG_BOUND;    /* everything bound */
        OS_STACK_BASE[i] = (void *)0;
    }
    memset(PROC_STATS_BASE, 0xEE, sizeof(PROC_STATS_BASE));
    n_lock = n_unlock = 0;
    last_lock_id = last_unlock_id = -1;
    n_ws_scan = 0;
    n_init_stack = 0;
    n_ts_timer = 0;
}

static int entry_marker, stack_marker;

/*
 * 0x00E14D40..0x00E14D76: slots 0, 1 and 2 are never considered even
 * when free; the first unbound slot at or above 3 wins.
 */
static void test_first_free_slot_from_three(void)
{
    status_$t st = -1;
    uint16_t pid;

    pcb_table[0].pri_max = 0;
    pcb_table[1].pri_max = 0;
    pcb_table[2].pri_max = 0;
    pcb_table[5].pri_max = 0;
    pcb_table[7].pri_max = 0;

    pid = PROC1_$BIND(&entry_marker, (void *)0x1234, &stack_marker, 5, &st);

    ASSERT_EQ(pid, 5);
    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(n_lock, 1);
    ASSERT_EQ(n_unlock, 1);
    ASSERT_EQ(last_lock_id, PROC1_CREATE_LOCK_ID);
    ASSERT_EQ(last_unlock_id, PROC1_CREATE_LOCK_ID);
}

/* 0x00E14D78..0x00E14DE2: every field the image writes, with its value */
static void test_pcb_initialisation(void)
{
    status_$t st;
    proc1_t *pcb = &pcb_table[9];
    uint16_t pid;

    pcb->pri_max = 0;
    pid = PROC1_$BIND(&entry_marker, (void *)0x1234, &stack_marker, 6, &st);
    ASSERT_EQ(pid, 9);

    /* 0x00E14D84: OS_STACK_BASE[pid] = stack_base (argument 3) */
    ASSERT_EQ((uintptr_t)OS_STACK_BASE[9], (uintptr_t)&stack_marker);

    /* 0x00E14D88: PMAP_$INIT_WS_SCAN(pid, ws_param), under the lock */
    ASSERT_EQ(n_ws_scan, 1);
    ASSERT_EQ(ws_scan_pid, 9);
    ASSERT_EQ(ws_scan_param, 6);
    ASSERT_EQ(unlocks_at_ws_scan, 0);

    /* 0x00E14DA2 */
    ASSERT_EQ(pcb->resource_locks_held, 0);
    /* 0x00E14DA6: 0x00010010 over (0x56) */
    ASSERT_EQ(pcb->inh_count, 0x0001);
    ASSERT_EQ(pcb->sw_bsr, 0x0010);
    /* 0x00E14DAE..0x00E14DBA: 00 00 00 00 00 00 00 10 over (0x4C..0x53) */
    ASSERT_EQ(pcb->cpu_total, 0);
    ASSERT_EQ(pcb->cpu_usage, 0);
    ASSERT_EQ(pcb->state, 0x0010);
    /* 0x00E14DBE */
    ASSERT_EQ(pcb->asid, 0);
    /* 0x00E14DD8: 0x000A over (0x54) */
    ASSERT_EQ(pcb->pri_min, 0);
    ASSERT_EQ(pcb->pri_max, PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED);
    /* 0x00E14DDE / 0x00E14DE2 */
    ASSERT_EQ(pcb->field_60, 0);
    ASSERT_EQ(pcb->field_64, 0);

    /* untouched neighbours keep their fill */
    ASSERT_EQ(pcb->nesting_depth, 0xA5A5);
    ASSERT_EQ(pcb->field_5c, 0xA5A5A5A5u);
    ASSERT_EQ(pcb->vtimer, (int16_t)0xA5A5);
    ASSERT_EQ(pcb->pad_4a, 0xA5A5);
}

/* 0x00E14DC2..0x00E14DD6: exactly the four longwords of entry pid */
static void test_stats_cleared_for_pid_only(void)
{
    status_$t st;

    pcb_table[4].pri_max = 0;
    (void)PROC1_$BIND(&entry_marker, (void *)0x1234, &stack_marker, 0, &st);

    ASSERT_EQ(PROC_STATS_BASE[4 * 4 + 0], 0);
    ASSERT_EQ(PROC_STATS_BASE[4 * 4 + 1], 0);
    ASSERT_EQ(PROC_STATS_BASE[4 * 4 + 2], 0);
    ASSERT_EQ(PROC_STATS_BASE[4 * 4 + 3], 0);
    ASSERT_EQ(PROC_STATS_BASE[3 * 4 + 3], 0xEEEEEEEEu);
    ASSERT_EQ(PROC_STATS_BASE[5 * 4 + 0], 0xEEEEEEEEu);
}

/*
 * 0x00E14DE6..0x00E14E0C: the lock is released before INIT_STACK, which
 * gets the addresses of arguments 1 and 2 (entry, initial_sp - NOT the
 * stack base), and PROC1_$INIT_TS_TIMER runs last.
 */
static void test_init_stack_and_timer_order(void)
{
    status_$t st;

    pcb_table[3].pri_max = 0;
    (void)PROC1_$BIND(&entry_marker, (void *)0x1234, &stack_marker, 0, &st);

    ASSERT_EQ(n_init_stack, 1);
    ASSERT_EQ(init_stack_pcb == &pcb_table[3], 1);
    ASSERT_EQ((uintptr_t)init_stack_entry, (uintptr_t)&entry_marker);
    ASSERT_EQ((uintptr_t)init_stack_sp, 0x1234);
    ASSERT_EQ(unlocks_at_init_stack, 1);
    ASSERT_EQ(pri_max_at_init_stack, PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED);

    ASSERT_EQ(n_ts_timer, 1);
    ASSERT_EQ(ts_timer_pid, 3);
    ASSERT_EQ(init_stacks_at_ts_timer, 1);
}

/* 0x00E14D52: slot 0x40 is the last one examined */
static void test_last_slot_is_0x40(void)
{
    status_$t st;
    uint16_t pid;

    pcb_table[0x40].pri_max = 0;
    pid = PROC1_$BIND(&entry_marker, (void *)0x1234, &stack_marker, 0, &st);

    ASSERT_EQ(pid, 0x40);
    ASSERT_EQ(st, status_$ok);
}

/*
 * 0x00E14D58..0x00E14D6A: with every slot bound the status is
 * no_pcb_is_available, the lock is released and nothing else happens.
 */
static void test_no_free_pcb(void)
{
    status_$t st = 0;

    (void)PROC1_$BIND(&entry_marker, (void *)0x1234, &stack_marker, 0, &st);

    ASSERT_EQ(st, status_$no_pcb_is_available);
    ASSERT_EQ(n_lock, 1);
    ASSERT_EQ(n_unlock, 1);
    ASSERT_EQ(n_ws_scan, 0);
    ASSERT_EQ(n_init_stack, 0);
    ASSERT_EQ(n_ts_timer, 0);
    ASSERT_EQ(pcb_table[0x40].pri_max, PROC1_FLAG_BOUND);
}

/* 0x00E14D70: only bit 3 of (0x55) decides; other flags do not free a slot */
static void test_only_bound_bit_matters(void)
{
    status_$t st;
    uint16_t pid;

    pcb_table[3].pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_WAITING;
    pcb_table[4].pri_max = PROC1_FLAG_SUSPENDED | PROC1_FLAG_DEFER_SUSP;
    pid = PROC1_$BIND(&entry_marker, (void *)0x1234, &stack_marker, 0, &st);

    ASSERT_EQ(pid, 4);
}

int main(void)
{
    RUN_TEST(test_first_free_slot_from_three);
    RUN_TEST(test_pcb_initialisation);
    RUN_TEST(test_stats_cleared_for_pid_only);
    RUN_TEST(test_init_stack_and_timer_order);
    RUN_TEST(test_last_slot_is_0x40);
    RUN_TEST(test_no_free_pcb);
    RUN_TEST(test_only_bound_bit_matters);

    printf("test_bind: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
