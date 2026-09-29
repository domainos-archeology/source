/*
 * Tests for the proc12 re-emission batch: PROC1_$LOADAV_CALLBACK
 * (0x00E14BDA), PROC1_$SET_TS (0x00E14A08), PROC1_$TS_END_CALLBACK
 * (0x00E14A70), PROC1_$SET_VT (0x00E1495C), PROC1_$VT_INT (0x00E1491E),
 * PROC1_$SET_PRIORITY (0x00E1523C), PROC1_$SET_TYPE (0x00E152E4),
 * PROC1_$SET_ASID (0x00E148F8), PROC1_$TST_LOCK (0x00E148CA),
 * PROC1_$SUSPENDP (0x00E14876), PROC1_$TRY_TO_SUSPEND (0x00E1471C) and
 * PROC1_$UNBIND (0x00E14E24).  Includes the real .c files with their
 * callees mocked.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"
#include "time/time.h"
#include "cal/cal.h"
#include "math/math.h"
#include "ec/ec.h"
#include "pmap/pmap.h"
#include "mmu/mmu.h"
#include "misc/misc.h"

int __host_intr_disable_count = 0;

/* module cells */
static proc1_t pcb_table[PROC1_MAX_PROCESSES];
proc1_t *PCBS[PROC1_MAX_PROCESSES];
proc1_t *PROC1_$CURRENT_PCB;
uint16_t PROC1_$READY_COUNT;
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];
ec_$eventcount_t PROC1_$SUSPEND_EC;
int16_t PROC1_$TSVV[PROC1_TSVV_COUNT] = {
    -1, -1, -1, -1, -1, -1, -1, 0x7D00, 0x7D00, 0x7D00, 0x7D00,
    0x30D4, 0x30D4, 0x30D4, 0x30D4, 0x30D4, -1, -1 };
const status_$t Illegal_process_id_err = status_$illegal_process_id;
const uint16_t PROC1_$VT_TIMER_DATA = 2;

/* mocks */
static int n_mul; static long mul_a, mul_b; static long mul_result;
long M$MIS$LLL(long a, long b) { n_mul++; mul_a = a; mul_b = b; return mul_result ? mul_result : a * b; }

void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t lo = (uint32_t)dst->low + src->low;
    dst->low = (uint16_t)lo;
    dst->high = dst->high + src->high + (lo >> 16);
}

static int n_reenter; static time_queue_t *re_q; static clock_t re_when; static int16_t re_flags;
static clock_t *re_base; static time_queue_elem_t *re_elem;
void TIME_$Q_REENTER_ELEM(time_queue_t *q, clock_t *when, int16_t qflags, clock_t *base,
                          time_queue_elem_t *elem, status_$t *status)
{
    n_reenter++; re_q = q; re_when = *when; re_flags = qflags; re_base = base; re_elem = elem;
    *status = status_$ok;
}

static int n_flush; static time_queue_t *flush_q; static int ipl_at_flush;
void TIME_$Q_FLUSH_QUEUE(time_queue_t *q) { n_flush++; flush_q = q; ipl_at_flush = __host_intr_disable_count; }

static uint16_t vt_now;
uint16_t TIME_$VT_TIMER(void) { return vt_now; }

static int n_wrt; static uint16_t wrt_index, wrt_value; static int ipl_at_wrt;
void TIME_$WRT_TIMER(uint16_t *index, uint16_t *value)
{
    n_wrt++; wrt_index = *index; wrt_value = *value; ipl_at_wrt = __host_intr_disable_count;
}

static int n_reorder, n_remove, n_add, n_dispatch; static proc1_t *last_reorder, *last_remove, *last_add;
static int order_tag; static int reorder_tag, remove_tag, add_tag;
void PROC1_$REORDER_READY(proc1_t *p) { n_reorder++; last_reorder = p; reorder_tag = ++order_tag; }
void PROC1_$REMOVE_READY(proc1_t *p) { n_remove++; last_remove = p; remove_tag = ++order_tag; }
void PROC1_$ADD_READY(proc1_t *p) { n_add++; last_add = p; add_tag = ++order_tag; }
static int ipl_at_dispatch;
void PROC1_$DISPATCH(void) { n_dispatch++; ipl_at_dispatch = __host_intr_disable_count; __host_intr_disable_count = 0; }

static int n_crash; static status_$t crash_status;
void CRASH_SYSTEM(const status_$t *s) { n_crash++; crash_status = *s; }

static int n_install_asid; static uint16_t installed_asid;
void MMU_$INSTALL_ASID(uint16_t a) { n_install_asid++; installed_asid = a; }

static int8_t inhibit_result;
int8_t PROC1_$INHIBIT_CHECK(proc1_t *p) { (void)p; return inhibit_result; }

static int n_advance; static ec_$eventcount_t *advanced;
void ADVANCE(ec_$eventcount_t *ec) { n_advance++; advanced = ec; }

static int n_purge; static int16_t purge_pid, purge_flags;
void PMAP_$PURGE_WS(int16_t pid, int16_t flags) { n_purge++; purge_pid = pid; purge_flags = flags; }

static int n_try; static int try_marks_suspended;
void PROC1_$TRY_TO_SUSPEND_mock(proc1_t *p)
{
    n_try++;
    if (try_marks_suspended) p->pri_max = (uint8_t)(p->pri_max | PROC1_FLAG_SUSPENDED);
}

static int n_suspend; static int8_t suspend_result; static status_$t suspend_status;
int8_t PROC1_$SUSPEND(uint16_t pid, status_$t *st) { (void)pid; n_suspend++; *st = suspend_status; return suspend_result; }

static int n_wait; static int32_t wait_val_seen; static ec_$eventcount_t *wait_ec_seen;
static proc1_t *wait_suspends;          /* the PCB EC_$WAIT marks suspended */
static int wait_suspends_after;         /* ... on this call number */
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    n_wait++; wait_val_seen = vals.val[0]; wait_ec_seen = ecs.ec[0];
    if (wait_suspends != NULL && n_wait >= wait_suspends_after) {
        wait_suspends->pri_max = (uint8_t)(wait_suspends->pri_max | PROC1_FLAG_SUSPENDED);
    }
    return 1;
}

static int n_free; static void *freed;
void PROC1_$FREE_STACK(void *s) { n_free++; freed = s; }

/* code under test */
#include "../loadav_callback.c"
#include "../set_ts.c"
#include "../ts_end_callback.c"
#include "../set_vt.c"
#include "../vt_int.c"
#include "../set_priority.c"
#include "../set_type.c"
#include "../set_asid.c"
#include "../tst_lock.c"
#include "../suspendp.c"
#include "../try_to_suspend.c"
#define PROC1_$TRY_TO_SUSPEND PROC1_$TRY_TO_SUSPEND_mock
#include "../unbind.c"
#undef PROC1_$TRY_TO_SUSPEND

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
#define ASSERT_PTR_EQ(a, b) ASSERT_EQ((uintptr_t)(a), (uintptr_t)(b))
#define RUN_TEST(fn) do { tests_run++; reset(); fn(); } while (0)

static void reset(void)
{
    unsigned i;
    memset(pcb_table, 0, sizeof(pcb_table));
    for (i = 0; i < PROC1_MAX_PROCESSES; i++) { PCBS[i] = &pcb_table[i]; pcb_table[i].mypid = (uint16_t)i; PROC1_$DATA.os_stack_base[i] = 0; }
    PROC1_$CURRENT_PCB = &pcb_table[3];
    PROC1_$READY_COUNT = 0;
    memset(PROC1_$DATA.loadav, 0, sizeof(PROC1_$DATA.loadav));
    memset(PROC1_$DATA.ts_elem, 0, sizeof(PROC1_$DATA.ts_elem));
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));
    memset(&PROC1_$SUSPEND_EC, 0, sizeof(PROC1_$SUSPEND_EC));
    __host_intr_disable_count = 0;
    n_mul = 0; mul_result = 0;
    n_reenter = n_flush = n_wrt = n_reorder = n_remove = n_add = n_dispatch = 0;
    order_tag = reorder_tag = remove_tag = add_tag = 0;
    n_crash = n_install_asid = n_advance = n_purge = n_try = n_suspend = n_wait = n_free = 0;
    inhibit_result = 0; try_marks_suspended = 1;
    suspend_result = -1; suspend_status = status_$ok;
    vt_now = 0;
    wait_suspends = NULL; wait_suspends_after = 0;
    ARCH_HOST_VA_BASE = 0;
}

/* ---- LOADAV_CALLBACK ---------------------------------------------- */

/* 0x00E14BEE..0x00E14C86: zero averages, ready count 3 */
static void test_loadav_from_zero(void)
{
    PROC1_$READY_COUNT = 3;
    PROC1_$LOADAV_CALLBACK();
    ASSERT_EQ(n_mul, 3);
    ASSERT_EQ(PROC1_$DATA.loadav[0], 3 * 0x1478);
    ASSERT_EQ(PROC1_$DATA.loadav[1], 3 * 0x043B);
    ASSERT_EQ(PROC1_$DATA.loadav[2], 3 * 0x016B);
}

/* the decay: (avg div 256) * decay div 256, with the last constants */
static void test_loadav_decay(void)
{
    PROC1_$READY_COUNT = 0;
    PROC1_$DATA.loadav[0] = 0x00100000;
    PROC1_$DATA.loadav[1] = 0x00100000;
    PROC1_$DATA.loadav[2] = 0x00100000;
    PROC1_$LOADAV_CALLBACK();
    ASSERT_EQ(PROC1_$DATA.loadav[0], (0x1000L * 0xEB88L) >> 8);
    ASSERT_EQ(PROC1_$DATA.loadav[1], (0x1000L * 0xFBC5L) >> 8);
    ASSERT_EQ(PROC1_$DATA.loadav[2], (0x1000L * 0xFE95L) >> 8);
    ASSERT_EQ(mul_b, 0xFE95);
}

/*
 * M$MIS$LLL hands back a 32-bit longword: 0x10000 * 0xEB88 = 0xEB880000 is
 * negative to the `bpl' at 0x00E14C0C, so the biased shift applies and the
 * average wraps.  The image does exactly this for a value of 0x01000000.
 */
static void test_loadav_product_wraps(void)
{
    PROC1_$READY_COUNT = 0;
    PROC1_$DATA.loadav[0] = 0x01000000;
    PROC1_$LOADAV_CALLBACK();
    ASSERT_EQ((uint32_t)PROC1_$DATA.loadav[0], 0xFFEB8800u);
}

/* `bpl / addi.l #0xff / asr.l #8': negatives round toward zero */
static void test_loadav_negative_rounding(void)
{
    PROC1_$READY_COUNT = 0;
    PROC1_$DATA.loadav[0] = -1;              /* -1 div 256 = 0, not -1 */
    PROC1_$DATA.loadav[1] = -256;            /* exactly -1 */
    PROC1_$DATA.loadav[2] = -257;            /* -1 (toward zero) */
    mul_result = 0;
    PROC1_$LOADAV_CALLBACK();
    ASSERT_EQ(PROC1_$DATA.loadav[0], 0);
    ASSERT_EQ((int32_t)PROC1_$DATA.loadav[1], (int32_t)(((-1L) * 0xFBC5L + 0xFF) >> 8));
    ASSERT_EQ((int32_t)PROC1_$DATA.loadav[2], (int32_t)(((-1L) * 0xFE95L + 0xFF) >> 8));
}

/* ---- SET_TS --------------------------------------------------------- */

/* 0x00E14A20..0x00E14A60 */
static void test_set_ts(void)
{
    proc1_t *p = &pcb_table[9];
    p->cpu_total = 0x11; p->cpu_usage = 0x22;
    PROC1_$SET_TS(p, 0x7D00);
    ASSERT_EQ(n_reenter, 1);
    ASSERT_PTR_EQ(re_q, &TIME_$VTQ[8]);
    ASSERT_EQ(re_when.high, 0);
    ASSERT_EQ(re_when.low, 0x7D00);
    ASSERT_EQ(re_flags, 0);
    ASSERT_PTR_EQ(re_base, &p->cpu_total);
    ASSERT_PTR_EQ(re_elem, &PROC1_$DATA.ts_elem[9].elem);
}

/* a negative timeslice is stored as its unsigned word */
static void test_set_ts_minus_one(void)
{
    PROC1_$SET_TS(&pcb_table[1], -1);
    ASSERT_EQ(re_when.low, 0xFFFF);
    ASSERT_PTR_EQ(re_q, &TIME_$VTQ[0]);
}

/* ---- TS_END_CALLBACK ------------------------------------------------ */

/* the element lives in an arena so its 32-bit VA fits the callback cell */
static uint8_t ts_arena[0x100];
static time_queue_elem_t *ts_elem;
static uint32_t ts_cell;

static void arm_callback(uint16_t pid)
{
    ARCH_HOST_VA_BASE = (uintptr_t)ts_arena;
    ts_elem = (time_queue_elem_t *)(ts_arena + 0x10);
    ts_elem->callback_arg = pid;
    ts_cell = ARCH_PTR_TO_VA(ts_elem);
}

/* 0x00E14AA8..0x00E14AE6: no locks -> remove + FIFO add; TSVV[state-1] */
static void test_ts_end_no_locks(void)
{
    proc1_t *p = &pcb_table[5];
    p->state = 0x0C; p->inh_count = 4;
    arm_callback(5);
    __host_intr_disable_count = 2;
    PROC1_$TS_END_CALLBACK(&ts_cell);
    ASSERT_EQ(p->state, 0x0B);
    ASSERT_EQ(n_remove, 1); ASSERT_PTR_EQ(last_remove, p);
    ASSERT_EQ(n_add, 1);    ASSERT_PTR_EQ(last_add, p);
    ASSERT_EQ(remove_tag < add_tag, 1);
    ASSERT_EQ(n_reorder, 0);
    ASSERT_EQ(p->pri_max & 0x10, 0);
    ASSERT_EQ(n_reenter, 1);
    ASSERT_EQ(re_when.low, 0x7D00);            /* TSVV[0x0B - 1] */
    ASSERT_PTR_EQ(re_q, &TIME_$VTQ[4]);
    /* SR restored before SET_TS, SET_TS sees the caller's IPL */
    ASSERT_EQ(__host_intr_disable_count, 2);
}

/* 0x00E14ABA..0x00E14AD0: locks held -> reorder + boosted flag */
static void test_ts_end_with_locks(void)
{
    proc1_t *p = &pcb_table[5];
    p->state = 0x10; p->inh_count = 1; p->resource_locks_held = 0x800;
    arm_callback(5);
    PROC1_$TS_END_CALLBACK(&ts_cell);
    ASSERT_EQ(n_reorder, 1); ASSERT_PTR_EQ(last_reorder, p);
    ASSERT_EQ(n_remove, 0); ASSERT_EQ(n_add, 0);
    ASSERT_EQ(p->pri_max & 0x10, 0x10);
    ASSERT_EQ(p->state, 0x0F);
    ASSERT_EQ(re_when.low, 0x30D4);            /* TSVV[0x0F - 1] */
}

/* 0x00E14AAC..0x00E14AB6: state is clamped up to inh_count */
static void test_ts_end_clamps_to_min(void)
{
    proc1_t *p = &pcb_table[5];
    p->state = 0x08; p->inh_count = 0x08;
    arm_callback(5);
    PROC1_$TS_END_CALLBACK(&ts_cell);
    ASSERT_EQ(p->state, 0x08);
    ASSERT_EQ(re_when.low, 0x7D00);            /* TSVV[7] */
}

/* 0x00E14A9A / 0x00E14AA0: pid 2 keeps its state and gets -1 */
static void test_ts_end_pid2(void)
{
    proc1_t *p = &pcb_table[2];
    p->state = 0x10;
    arm_callback(2);
    PROC1_$TS_END_CALLBACK(&ts_cell);
    ASSERT_EQ(p->state, 0x10);
    ASSERT_EQ(n_remove + n_add + n_reorder, 0);
    ASSERT_EQ(re_when.low, 0xFFFF);
    ASSERT_PTR_EQ(re_q, &TIME_$VTQ[1]);
}

/* ---- SET_VT --------------------------------------------------------- */

/* 0x00E149F8: not the current process - store only, status untouched */
static void test_set_vt_other_process(void)
{
    status_$t st = 0x5555;
    clock_t vt = { 0, 0x1234 };
    pcb_table[7].pri_max = PROC1_FLAG_BOUND;
    PROC1_$SET_VT(7, &vt, &st);
    ASSERT_EQ(pcb_table[7].vtimer, 0x1234);
    ASSERT_EQ(st, 0x5555);
    ASSERT_EQ(n_wrt, 0);
}

/* 0x00E149A0 / 0x00E149A4: a non-zero high longword saturates */
static void test_set_vt_saturates(void)
{
    status_$t st = 0;
    clock_t vt = { 1, 0x0001 };
    pcb_table[7].pri_max = PROC1_FLAG_BOUND;
    PROC1_$SET_VT(7, &vt, &st);
    ASSERT_EQ((uint16_t)pcb_table[7].vtimer, 0xFFFF);
}

/* 0x00E149B6..0x00E149F4: current process folds the elapsed time first */
static void test_set_vt_current_process(void)
{
    status_$t st = 0x5555;
    clock_t vt = { 0, 0x0100 };
    proc1_t *p = &pcb_table[3];
    p->pri_max = PROC1_FLAG_BOUND;
    p->vtimer = 0x0300; vt_now = 0x0100;
    p->cpu_total = 5; p->cpu_usage = 0xFF00;
    __host_intr_disable_count = 0;
    PROC1_$SET_VT(3, &vt, &st);
    ASSERT_EQ(p->cpu_total, 6);
    ASSERT_EQ(p->cpu_usage, 0x0100);
    ASSERT_EQ(p->vtimer, 0x0100);
    ASSERT_EQ(n_wrt, 1);
    ASSERT_EQ(wrt_index, 2);
    ASSERT_EQ(wrt_value, 0x0100);
    ASSERT_EQ(ipl_at_wrt, 1);
    ASSERT_EQ(__host_intr_disable_count, 0);
    ASSERT_EQ(st, 0x5555);
}

/* 0x00E14978 / 0x00E14998 */
static void test_set_vt_errors(void)
{
    status_$t st;
    clock_t vt = { 0, 1 };
    PROC1_$SET_VT(0, &vt, &st);    ASSERT_EQ(st, status_$illegal_process_id);
    PROC1_$SET_VT(0x41, &vt, &st); ASSERT_EQ(st, status_$illegal_process_id);
    PROC1_$SET_VT(4, &vt, &st);    ASSERT_EQ(st, status_$process_not_bound);
}

/* ---- VT_INT --------------------------------------------------------- */

/* 0x00E1492A..0x00E1494E */
static void test_vt_int(void)
{
    clock_t out;
    proc1_t *p = &pcb_table[3];
    p->vtimer = 0x0010; p->cpu_total = 1; p->cpu_usage = 0xFFF8;
    PROC1_$VT_INT(&out);
    ASSERT_EQ(p->vtimer, 0);
    ASSERT_EQ(out.high, 2);
    ASSERT_EQ(out.low, 0x0008);
}

/* ---- SET_PRIORITY --------------------------------------------------- */

/* 0x00E152CE: query */
static void test_set_priority_query(void)
{
    uint16_t lo = 0, hi = 0;
    pcb_table[6].inh_count = 3; pcb_table[6].sw_bsr = 9;
    PROC1_$SET_PRIORITY(6, 0, &lo, &hi);
    ASSERT_EQ(lo, 3); ASSERT_EQ(hi, 9);
    ASSERT_EQ(n_reorder, 0);
}

/* 0x00E1527C..0x00E152C8: set, clamps, state pulled down to max, reorder */
static void test_set_priority_set_clamps_max(void)
{
    uint16_t lo = 0, hi = 0x30;
    proc1_t *p = &pcb_table[6];
    p->state = 0x12; p->pri_max = PROC1_FLAG_BOUND;
    PROC1_$SET_PRIORITY(6, -1, &lo, &hi);
    ASSERT_EQ(p->inh_count, 1);
    ASSERT_EQ(p->sw_bsr, 0x10);
    ASSERT_EQ(p->state, 0x10);
    ASSERT_EQ(n_reorder, 1); ASSERT_PTR_EQ(last_reorder, p);
    ASSERT_EQ(n_dispatch, 1);
    ASSERT_EQ(ipl_at_dispatch, 1);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

/* state pulled up to min; not on the ready list -> no reorder */
static void test_set_priority_set_clamps_min(void)
{
    uint16_t lo = 7, hi = 12;
    proc1_t *p = &pcb_table[6];
    p->state = 2; p->pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED;
    PROC1_$SET_PRIORITY(6, -1, &lo, &hi);
    ASSERT_EQ(p->state, 7);
    ASSERT_EQ(n_reorder, 0);
    ASSERT_EQ(n_dispatch, 0);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

/* 0x00E1525E: bad pid crashes and then carries on */
static void test_set_priority_bad_pid_crashes(void)
{
    uint16_t lo = 0, hi = 0;
    pcb_table[0].inh_count = 0x33;
    PROC1_$SET_PRIORITY(0, 0, &lo, &hi);
    ASSERT_EQ(n_crash, 1);
    ASSERT_EQ(crash_status, status_$illegal_process_id);
    ASSERT_EQ(lo, 0x33);
}

/* ---- SET_TYPE / SET_ASID / TST_LOCK --------------------------------- */

static void test_set_type(void)
{
    PROC1_$SET_TYPE(0x40, 7);
    ASSERT_EQ(PROC1_$DATA.type[0x40], 7);
    ASSERT_EQ(n_crash, 0);
    PROC1_$SET_TYPE(0x41, 9);
    ASSERT_EQ(n_crash, 1);
    ASSERT_EQ(crash_status, status_$illegal_process_id);
}

static void test_set_asid(void)
{
    PROC1_$SET_ASID(0x2A);
    ASSERT_EQ(pcb_table[3].asid, 0x2A);
    ASSERT_EQ(n_install_asid, 1);
    ASSERT_EQ(installed_asid, 0x2A);
}

/* 0x00E148DC: btst.l Dn takes the bit number modulo 32 */
static void test_tst_lock(void)
{
    pcb_table[3].resource_locks_held = 0x80000800;
    ASSERT_EQ((uint8_t)PROC1_$TST_LOCK(11), 0xFF);
    ASSERT_EQ((uint8_t)PROC1_$TST_LOCK(31), 0xFF);
    ASSERT_EQ((uint8_t)PROC1_$TST_LOCK(10), 0x00);
    ASSERT_EQ((uint8_t)PROC1_$TST_LOCK(32 + 11), 0xFF);
}

/* ---- SUSPENDP ------------------------------------------------------- */

static void test_suspendp(void)
{
    status_$t st;
    ASSERT_EQ((uint8_t)PROC1_$SUSPENDP(0, &st), 0xFF);
    ASSERT_EQ(st, status_$illegal_process_id);
    ASSERT_EQ((uint8_t)PROC1_$SUSPENDP(5, &st), 0xFF);
    ASSERT_EQ(st, status_$process_not_bound);
    pcb_table[5].pri_max = PROC1_FLAG_BOUND;
    ASSERT_EQ((uint8_t)PROC1_$SUSPENDP(5, &st), 0x00);
    ASSERT_EQ(st, status_$ok);
    pcb_table[5].pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED;
    ASSERT_EQ((uint8_t)PROC1_$SUSPENDP(5, &st), 0xFF);
}

/* ---- TRY_TO_SUSPEND ------------------------------------------------- */

/* 0x00E14726..0x00E14760: not inhibited, on the ready list */
static void test_try_to_suspend_now(void)
{
    proc1_t *p = &pcb_table[5];
    p->pri_min = 0x5A; p->pri_max = PROC1_FLAG_BOUND;
    PROC1_$TRY_TO_SUSPEND(p);
    ASSERT_EQ(n_remove, 1); ASSERT_PTR_EQ(last_remove, p);
    ASSERT_EQ(p->pri_min, 0x5A);                     /* word store keeps it */
    ASSERT_EQ(p->pri_max, PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED);
    ASSERT_EQ(n_advance, 1); ASSERT_PTR_EQ(advanced, &PROC1_$SUSPEND_EC);
}

/* 0x00E1473A: a waiting process is not on the ready list */
static void test_try_to_suspend_waiting(void)
{
    proc1_t *p = &pcb_table[5];
    p->pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_WAITING;
    PROC1_$TRY_TO_SUSPEND(p);
    ASSERT_EQ(n_remove, 0);
    ASSERT_EQ(p->pri_max, PROC1_FLAG_BOUND | PROC1_FLAG_WAITING | PROC1_FLAG_SUSPENDED);
    ASSERT_EQ(n_advance, 1);
}

/* 0x00E14736: inhibited - only DEFER_SUSP is set */
static void test_try_to_suspend_inhibited(void)
{
    proc1_t *p = &pcb_table[5];
    p->pri_max = PROC1_FLAG_BOUND;
    inhibit_result = -1;
    PROC1_$TRY_TO_SUSPEND(p);
    ASSERT_EQ(p->pri_max, PROC1_FLAG_BOUND | PROC1_FLAG_DEFER_SUSP);
    ASSERT_EQ(n_remove, 0);
    ASSERT_EQ(n_advance, 0);
}

/* ---- UNBIND --------------------------------------------------------- */

static void test_unbind_errors(void)
{
    status_$t st;
    PROC1_$UNBIND(0, &st);    ASSERT_EQ(st, status_$illegal_process_id);
    PROC1_$UNBIND(0x41, &st); ASSERT_EQ(st, status_$illegal_process_id);
    PROC1_$UNBIND(8, &st);    ASSERT_EQ(st, status_$process_not_bound);
    ASSERT_EQ(n_purge + n_flush + n_free, 0);
}

/* 0x00E14E74..0x00E14F3E: self */
static void test_unbind_self(void)
{
    status_$t st = 0x7777;
    proc1_t *p = &pcb_table[3];
    p->pri_max = PROC1_FLAG_BOUND;
    PROC1_$DATA.os_stack_base[3] = 0x00D40000;   /* a stack top VA */
    PROC1_$DATA.type[3] = 5;
    PROC1_$UNBIND(3, &st);
    ASSERT_EQ(n_purge, 1); ASSERT_EQ(purge_pid, 3); ASSERT_EQ(purge_flags, 0);
    ASSERT_EQ(n_try, 1);
    ASSERT_EQ(n_crash, 0);
    ASSERT_EQ(n_flush, 1); ASSERT_PTR_EQ(flush_q, &TIME_$VTQ[2]);
    ASSERT_EQ(ipl_at_flush, 1);
    ASSERT_EQ(p->pri_max & PROC1_FLAG_BOUND, 0);
    ASSERT_EQ(n_free, 1); ASSERT_PTR_EQ(freed, ARCH_VA_TO_PTR(0x00D40000));
    ASSERT_EQ(PROC1_$DATA.type[3], 0);
    ASSERT_EQ(n_dispatch, 1); ASSERT_EQ(ipl_at_dispatch, 1);
    ASSERT_EQ(n_suspend + n_wait, 0);
    ASSERT_EQ(st, 0x7777);                       /* status never written */
}

/* 0x00E14E94: self, but TRY_TO_SUSPEND could not - crash 0xA000A */
static void test_unbind_self_not_suspendable(void)
{
    status_$t st;
    pcb_table[3].pri_max = PROC1_FLAG_BOUND;
    try_marks_suspended = 0;
    PROC1_$UNBIND(3, &st);
    ASSERT_EQ(n_crash, 1);
    ASSERT_EQ(crash_status, status_$process_not_suspendable);
    ASSERT_EQ(n_flush, 1);                       /* and carries on */
}

/* 0x00E14EAA..0x00E14EF0: another process that suspends at once */
static void test_unbind_other_immediate(void)
{
    status_$t st;
    proc1_t *p = &pcb_table[9];
    p->pri_max = PROC1_FLAG_BOUND;
    PROC1_$SUSPEND_EC.value = 40;
    suspend_result = -1;
    PROC1_$UNBIND(9, &st);
    ASSERT_EQ(n_suspend, 1);
    ASSERT_EQ(n_wait, 0);
    ASSERT_EQ(n_purge, 1); ASSERT_EQ(purge_pid, 9);
    ASSERT_EQ(n_flush, 1); ASSERT_PTR_EQ(flush_q, &TIME_$VTQ[8]);
    ASSERT_EQ(n_try, 0);
}

/* 0x00E14ECA..0x00E14EF2: EC_$WAIT for value+1 until SUSPENDP says true */
static void test_unbind_other_waits(void)
{
    status_$t st;
    proc1_t *p = &pcb_table[9];
    p->pri_max = PROC1_FLAG_BOUND;
    PROC1_$SUSPEND_EC.value = 40;
    suspend_result = 0;                  /* not suspended yet */
    wait_suspends = p; wait_suspends_after = 2;
    PROC1_$UNBIND(9, &st);
    ASSERT_EQ(n_suspend, 1);
    ASSERT_EQ(n_wait, 2);
    ASSERT_EQ(wait_val_seen, 41);        /* sampled value + 1, both times */
    ASSERT_PTR_EQ(wait_ec_seen, &PROC1_$SUSPEND_EC);
    ASSERT_EQ(st, status_$ok);           /* SUSPENDP's status */
    ASSERT_EQ(n_purge, 1);
    ASSERT_EQ(n_flush, 1);
}

/* 0x00E14EA2: another process already suspended - no suspend, no wait */
static void test_unbind_other_already_suspended(void)
{
    status_$t st = 0x1111;
    pcb_table[9].pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED;
    PROC1_$UNBIND(9, &st);
    ASSERT_EQ(n_suspend, 0); ASSERT_EQ(n_wait, 0);
    ASSERT_EQ(n_purge, 1);
    ASSERT_EQ(st, 0x1111);
}

int main(void)
{
    RUN_TEST(test_loadav_from_zero);
    RUN_TEST(test_loadav_decay);
    RUN_TEST(test_loadav_product_wraps);
    RUN_TEST(test_loadav_negative_rounding);
    RUN_TEST(test_set_ts);
    RUN_TEST(test_set_ts_minus_one);
    RUN_TEST(test_ts_end_no_locks);
    RUN_TEST(test_ts_end_with_locks);
    RUN_TEST(test_ts_end_clamps_to_min);
    RUN_TEST(test_ts_end_pid2);
    RUN_TEST(test_set_vt_other_process);
    RUN_TEST(test_set_vt_saturates);
    RUN_TEST(test_set_vt_current_process);
    RUN_TEST(test_set_vt_errors);
    RUN_TEST(test_vt_int);
    RUN_TEST(test_set_priority_query);
    RUN_TEST(test_set_priority_set_clamps_max);
    RUN_TEST(test_set_priority_set_clamps_min);
    RUN_TEST(test_set_priority_bad_pid_crashes);
    RUN_TEST(test_set_type);
    RUN_TEST(test_set_asid);
    RUN_TEST(test_tst_lock);
    RUN_TEST(test_suspendp);
    RUN_TEST(test_try_to_suspend_now);
    RUN_TEST(test_try_to_suspend_waiting);
    RUN_TEST(test_try_to_suspend_inhibited);
    RUN_TEST(test_unbind_errors);
    RUN_TEST(test_unbind_self);
    RUN_TEST(test_unbind_self_not_suspendable);
    RUN_TEST(test_unbind_other_immediate);
    RUN_TEST(test_unbind_other_waits);
    RUN_TEST(test_unbind_other_already_suspended);
    printf("test_scheduling: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
