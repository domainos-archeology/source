/*
 * time/test/test_q_scan_queue.c - Unit tests for TIME_$Q_SCAN_QUEUE
 * (0x00E16E94), plus TIME_$GET_EC (0x00E1670A) and TIME_$INIT (0x00E2FE6C)
 *
 * The real .c files are #included.  ML_$SPIN_LOCK/UNLOCK, SUB48/ADD48,
 * time_$q_insert_sorted, time_$q_setup_timer, DXM_$ADD_CALLBACK and the
 * INIT/GET_EC callees are mocked and record what they were handed.  Queue
 * links are 32-bit target virtual addresses, so ARCH_HOST_VA_BASE points at
 * a local arena holding the elements.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "time/time_internal.h"
#include "dxm/dxm.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

#define ASSERT_PTR_EQ(expected, actual)                                       \
    do {                                                                      \
        const void *_e = (const void *)(expected);                            \
        const void *_a = (const void *)(actual);                              \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: %p, Got: %p at line %d\n",          \
                   _e, _a, __LINE__);                                         \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];
time_queue_t TIME_$RTEQ;
di_queue_elem_t TIME_$DI_VT;
di_queue_elem_t TIME_$DI_RTE;
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */
uint16_t TIME_$CLOCKL;
uint32_t TIME_$CURRENT_CLOCKH;
uint16_t TIME_$CURRENT_CLOCKL;
uint32_t TIME_$BOOT_TIME;
uint32_t TIME_$CURRENT_TIME;
uint32_t TIME_$CURRENT_USEC;
uint16_t TIME_$CURRENT_TICK;
uint16_t TIME_$CURRENT_SKEW;
uint32_t TIME_$CURRENT_DELTA;
ec_$eventcount_t TIME_$FAST_CLOCK_EC;
void *time_$fast_clock_ec_handle;
void *time_$clock_ec_handle;
dxm_queue_t DXM_$WIRED_Q;
dxm_queue_t DXM_$UNWIRED_Q;

/* Arena standing in for target memory; queue elements live inside it. */
static uint8_t arena[0x1000];
#define ELEM_A_VA   0x100
#define ELEM_B_VA   0x200
#define ELEM_C_VA   0x300

static time_queue_elem_t *elem_at(uint32_t va)
{
    return (time_queue_elem_t *)ARCH_VA_TO_PTR(va);
}

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int lock_calls, unlock_calls;
static void *lock_target;
static ml_$spin_token_t last_unlock_token;
static int lock_depth;          /* +1 on lock, -1 on unlock */
static int lock_depth_at_direct_call;
static int lock_depth_at_dxm_call;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    lock_calls++;
    lock_target = lockp;
    lock_depth++;
    return (ml_$spin_token_t)(0x0700 + lock_calls);
}

void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot)
{
    ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token;
    unlock_calls++;
    lock_target = lockp;
    lock_depth--;
    last_unlock_token = token;
}

/* Real 48-bit arithmetic, reproduced so the test stays one translation unit. */
int8_t SUB48(clock_t *dst, clock_t *src)
{
    uint16_t dst_low = dst->low;
    uint16_t src_low = src->low;

    dst->low = (uint16_t)(dst_low - src_low);
    dst->high = dst->high - src->high - (dst_low < src_low ? 1u : 0u);
    return ((int32_t)dst->high >= 0) ? -1 : 0;
}

void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t low = (uint32_t)dst->low + (uint32_t)src->low;
    dst->low = (uint16_t)low;
    dst->high = dst->high + src->high + (low >> 16);
}

static int insert_calls;
static time_queue_elem_t *insert_elem;

int8_t time_$q_insert_sorted(time_queue_t *queue, time_queue_elem_t *elem)
{
    insert_calls++;
    insert_elem = elem;
    /* push it back on the head so the scan sees it again */
    elem->next = queue->head;
    queue->head = ARCH_PTR_TO_VA(elem);
    elem->flags |= TIME_QELEM_IN_QUEUE;
    return 0;
}

static int setup_calls;
static time_queue_t *setup_queue;
static clock_t setup_now;

void time_$q_setup_timer(time_queue_t *queue, clock_t *now)
{
    setup_calls++;
    setup_queue = queue;
    setup_now = *now;
}

static int dxm_calls;
static dxm_queue_t *dxm_queue_arg;
static const dxm_$callback_t *dxm_callback_arg;
static void *dxm_data_value;
static uint16_t dxm_size_arg;
static boolean dxm_check_dup_arg;
static status_$t *dxm_status_arg;

void DXM_$ADD_CALLBACK(dxm_queue_t *queue, const dxm_$callback_t *callback,
                       void **data, uint16_t data_size,
                       boolean check_dup, status_$t *status_ret)
{
    dxm_calls++;
    dxm_queue_arg = queue;
    dxm_callback_arg = callback;
    dxm_data_value = *data;
    dxm_size_arg = data_size;
    dxm_check_dup_arg = check_dup;
    dxm_status_arg = status_ret;
    lock_depth_at_dxm_call = lock_depth;
    *status_ret = 0x00170002;
}

/* The direct callback under test */
static int direct_calls;
static time_queue_elem_t *direct_elem;

static void direct_callback(void *arg)
{
    direct_calls++;
    direct_elem = *(time_queue_elem_t **)arg;
    lock_depth_at_direct_call = lock_depth;
}

/* Host DXM callback-cell registry (see dxm/dxm.h) */
static dxm_$callback_fn_t registry[4];
static uint32_t registry_count;

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    registry[registry_count] = fn;
    registry_count++;
    return (dxm_$callback_t)registry_count;
}

dxm_$callback_fn_t dxm_$callback_fn(dxm_$callback_t cell)
{
    if (cell == 0 || cell > registry_count) {
        return NULL;
    }
    return registry[cell - 1];
}

/* INIT / GET_EC callees */
static int q_init_calls;
void TIME_$Q_INIT(void) { q_init_calls++; }

static int init_queue_calls;
static boolean init_queue_vt[65];
static uint16_t init_queue_id[65];
static time_queue_t *init_queue_ptr[65];

void TIME_$Q_INIT_QUEUE(boolean is_vt, uint16_t queue_id, time_queue_t *queue)
{
    if (init_queue_calls < 65) {
        init_queue_vt[init_queue_calls] = is_vt;
        init_queue_id[init_queue_calls] = queue_id;
        init_queue_ptr[init_queue_calls] = queue;
    }
    init_queue_calls++;
}

static int di_init_calls;
static di_queue_elem_t *di_init_elems[2];
void DI_$INIT_Q_ELEM(di_queue_elem_t *elem)
{
    if (di_init_calls < 2) {
        di_init_elems[di_init_calls] = elem;
    }
    di_init_calls++;
}

static int read_cal_calls;
static clock_t read_cal_clock = { 0x11223344, 0x5566 };
static uint32_t read_cal_time = 1000;
void TIME_$READ_CAL(clock_t *clock, uint32_t *time)
{
    read_cal_calls++;
    *clock = read_cal_clock;
    *time = read_cal_time;
}

static int timer_init_calls;
int32_t TIMER_$INIT(void) { timer_init_calls++; return 0; }

static int register_calls;
static ec_$eventcount_t *register_ec[2];
static status_$t register_status_value;
static uint8_t handle_a, handle_b;
void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret)
{
    if (register_calls < 2) {
        register_ec[register_calls] = ec1;
    }
    register_calls++;
    *status_ret = register_status_value;
    return (register_calls == 1) ? (void *)&handle_a : (void *)&handle_b;
}

static void reset(void)
{
    memset(arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    memset(&TIME_$RTEQ, 0, sizeof(TIME_$RTEQ));
    lock_calls = unlock_calls = 0;
    lock_depth = 0;
    lock_depth_at_direct_call = lock_depth_at_dxm_call = -99;
    insert_calls = 0;
    setup_calls = 0;
    dxm_calls = 0;
    direct_calls = 0;
    direct_elem = NULL;
    registry_count = 0;
    q_init_calls = init_queue_calls = di_init_calls = 0;
    read_cal_calls = timer_init_calls = register_calls = 0;
    register_status_value = status_$ok;
    time_$fast_clock_ec_handle = NULL;
    time_$clock_ec_handle = NULL;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../q_scan_queue.c"
#include "../get_ec.c"
#include "../init.c"

/* Build an element in the arena. */
static time_queue_elem_t *make_elem(uint32_t va, uint32_t exp_high,
                                    uint16_t exp_low, uint16_t flags)
{
    time_queue_elem_t *e = elem_at(va);

    memset(e, 0, sizeof(*e));
    e->callback = dxm_$callback_cell(direct_callback);
    e->callback_arg = 0xCAFE0000u | (va & 0xFFFF);
    e->expire_high = exp_high;
    e->expire_low = exp_low;
    e->flags = (uint16_t)(flags | TIME_QELEM_IN_QUEUE);
    e->interval_high = 0;
    e->interval_low = 0x10;
    return e;
}

/* ==========================================================================
 * TIME_$Q_SCAN_QUEUE
 * ========================================================================== */

TEST(scan_empty_queue_only_locks)
{
    clock_t now = { 10, 0 };
    status_$t status = 0x5555;

    reset();
    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_PTR_EQ(&TIME_$RTEQ.lock, lock_target);
    ASSERT_EQ(0x0701, last_unlock_token);
    ASSERT_EQ(0, setup_calls);
    ASSERT_EQ(0, direct_calls);
    ASSERT_EQ(0x5555, status);
}

/* Head in the future: nothing fires, the timer is re-armed for it. */
TEST(scan_future_head_rearms_timer)
{
    clock_t now = { 10, 0x100 };
    status_$t status = 0;
    time_queue_elem_t *a;

    reset();
    a = make_elem(ELEM_A_VA, 10, 0x101, 0);
    TIME_$RTEQ.head = ELEM_A_VA;

    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    ASSERT_EQ(ELEM_A_VA, TIME_$RTEQ.head);
    ASSERT_EQ(TIME_QELEM_IN_QUEUE, a->flags);
    ASSERT_EQ(0, direct_calls);
    ASSERT_EQ(1, setup_calls);
    ASSERT_PTR_EQ(&TIME_$RTEQ, setup_queue);
    ASSERT_EQ(10, setup_now.high);
    ASSERT_EQ(0x100, setup_now.low);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
}

/* An element whose expiry EQUALS now fires (the cmpm path), with the lock
 * held, and is handed the address of a cell holding the element. */
TEST(scan_direct_fires_equal_expiry_under_lock)
{
    clock_t now = { 10, 0x100 };
    status_$t status = 0;
    time_queue_elem_t *a;

    reset();
    a = make_elem(ELEM_A_VA, 10, 0x100, 0);
    TIME_$RTEQ.head = ELEM_A_VA;

    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    ASSERT_EQ(1, direct_calls);
    ASSERT_PTR_EQ(a, direct_elem);
    ASSERT_EQ(1, lock_depth_at_direct_call);   /* still locked */
    ASSERT_EQ(0, TIME_$RTEQ.head);
    ASSERT_EQ(0, a->next);
    ASSERT_EQ(0, a->flags);                     /* in-queue bit cleared */
    ASSERT_EQ(0, setup_calls);                  /* queue now empty */
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, status);                       /* direct path never writes it */
}

/* Two expired elements then one in the future: both fire in order, the
 * third stays and re-arms the timer. */
TEST(scan_fires_all_expired_then_stops)
{
    clock_t now = { 20, 0 };
    status_$t status = 0;
    time_queue_elem_t *a;
    time_queue_elem_t *b;
    time_queue_elem_t *c;

    reset();
    a = make_elem(ELEM_A_VA, 19, 0xFFFF, 0);
    b = make_elem(ELEM_B_VA, 5, 0, 0);
    c = make_elem(ELEM_C_VA, 20, 1, 0);
    TIME_$RTEQ.head = ELEM_A_VA;
    a->next = ELEM_B_VA;
    b->next = ELEM_C_VA;

    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    ASSERT_EQ(2, direct_calls);
    ASSERT_PTR_EQ(b, direct_elem);
    ASSERT_EQ(ELEM_C_VA, TIME_$RTEQ.head);
    ASSERT_EQ(0, a->next);
    ASSERT_EQ(0, b->next);
    ASSERT_EQ(TIME_QELEM_IN_QUEUE, c->flags);
    ASSERT_EQ(1, setup_calls);
}

/* Repeating element: expiry += interval and re-insert before it fires. */
TEST(scan_repeat_reinserts)
{
    clock_t now = { 20, 0 };
    status_$t status = 0;
    time_queue_elem_t *a;

    reset();
    a = make_elem(ELEM_A_VA, 19, 0xFFF8, TIME_QELEM_REPEAT);
    TIME_$RTEQ.head = ELEM_A_VA;

    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    ASSERT_EQ(1, insert_calls);
    ASSERT_PTR_EQ(a, insert_elem);
    ASSERT_EQ(20, a->expire_high);              /* 19:FFF8 + 0:10 */
    ASSERT_EQ(0x0008, a->expire_low);
    ASSERT_EQ(1, direct_calls);
    /* the mock re-inserted it at the head; 20:8 > 20:0 so the scan stops */
    ASSERT_EQ(ELEM_A_VA, TIME_$RTEQ.head);
    ASSERT_EQ(1, setup_calls);
}

/* Bit 2: DXM_$UNWIRED_Q, lock released around the call and re-taken. */
TEST(scan_unwired_defers_through_dxm)
{
    clock_t now = { 20, 0 };
    status_$t status = 0;
    time_queue_elem_t *a;

    reset();
    a = make_elem(ELEM_A_VA, 1, 0, TIME_QELEM_UNWIRED | TIME_QELEM_CHECK_DUP);
    TIME_$RTEQ.head = ELEM_A_VA;

    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    ASSERT_EQ(1, dxm_calls);
    ASSERT_PTR_EQ(&DXM_$UNWIRED_Q, dxm_queue_arg);
    ASSERT_PTR_EQ(&a->callback, dxm_callback_arg);
    ASSERT_PTR_EQ(&a->callback_arg, dxm_data_value);
    ASSERT_EQ(4, dxm_size_arg);
    ASSERT_EQ((int8_t)0xFF, dxm_check_dup_arg);
    ASSERT_PTR_EQ(&status, dxm_status_arg);
    ASSERT_EQ(0x00170002, status);
    ASSERT_EQ(0, lock_depth_at_dxm_call);       /* unlocked around DXM */
    ASSERT_EQ(0, direct_calls);
    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(2, unlock_calls);
    ASSERT_EQ(0x0702, last_unlock_token);       /* the re-taken lock's token */
    ASSERT_EQ(0, lock_depth);
}

/* Bit 3 alone: DXM_$WIRED_Q; check_dup false when bit 4 is clear. */
TEST(scan_wired_defers_through_dxm)
{
    clock_t now = { 20, 0 };
    status_$t status = 0;

    reset();
    make_elem(ELEM_A_VA, 1, 0, TIME_QELEM_WIRED);
    TIME_$RTEQ.head = ELEM_A_VA;

    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    ASSERT_EQ(1, dxm_calls);
    ASSERT_PTR_EQ(&DXM_$WIRED_Q, dxm_queue_arg);
    ASSERT_EQ(0, dxm_check_dup_arg);
}

/* Both bits: bit 2 wins. */
TEST(scan_both_bits_prefers_unwired)
{
    clock_t now = { 20, 0 };
    status_$t status = 0;

    reset();
    make_elem(ELEM_A_VA, 1, 0, TIME_QELEM_WIRED | TIME_QELEM_UNWIRED);
    TIME_$RTEQ.head = ELEM_A_VA;

    TIME_$Q_SCAN_QUEUE(&TIME_$RTEQ, &now, &status);

    ASSERT_EQ(1, dxm_calls);
    ASSERT_PTR_EQ(&DXM_$UNWIRED_Q, dxm_queue_arg);
}

/* ==========================================================================
 * TIME_$GET_EC
 * ========================================================================== */

TEST(get_ec_registers_once_and_returns_clock_handle)
{
    uint16_t id = 0;
    void *ret = NULL;
    status_$t status = 0x1234;

    reset();
    TIME_$GET_EC(&id, &ret, &status);

    ASSERT_EQ(2, register_calls);
    ASSERT_PTR_EQ(&TIME_$CLOCKH, register_ec[0]);
    ASSERT_PTR_EQ(&TIME_$FAST_CLOCK_EC, register_ec[1]);
    ASSERT_PTR_EQ(&handle_a, time_$clock_ec_handle);
    ASSERT_PTR_EQ(&handle_b, time_$fast_clock_ec_handle);
    ASSERT_PTR_EQ(&handle_a, ret);
    ASSERT_EQ(status_$ok, status);

    /* second call: cached, no more registrations */
    id = 1;
    TIME_$GET_EC(&id, &ret, &status);
    ASSERT_EQ(2, register_calls);
    ASSERT_PTR_EQ(&handle_b, ret);
    ASSERT_EQ(status_$ok, status);
}

TEST(get_ec_bad_key)
{
    uint16_t id = 2;
    void *ret = (void *)&id;
    status_$t status = 0;

    reset();
    time_$clock_ec_handle = &handle_a;
    time_$fast_clock_ec_handle = &handle_b;

    TIME_$GET_EC(&id, &ret, &status);

    ASSERT_EQ(0, register_calls);
    ASSERT_EQ(status_$time_bad_timer_key, status);
    ASSERT_EQ(0x000D0005, status);
    ASSERT_PTR_EQ(&id, ret);                    /* untouched */
}

/* A registration failure is only looked at after both registrations. */
TEST(get_ec_registration_failure)
{
    uint16_t id = 0;
    void *ret = NULL;
    status_$t status = 0;

    reset();
    register_status_value = 0x00120003;

    TIME_$GET_EC(&id, &ret, &status);

    ASSERT_EQ(2, register_calls);
    ASSERT_EQ(0x00120003, status);
    ASSERT_PTR_EQ(NULL, ret);
}

/* ==========================================================================
 * TIME_$INIT
 * ========================================================================== */

TEST(init_with_calendar)
{
    uint8_t flag = 0xFF;

    reset();
    TIME_$CURRENT_SKEW = 7;
    TIME_$CURRENT_DELTA = 9;
    TIME_$CURRENT_USEC = 11;

    TIME_$INIT(&flag);

    ASSERT_EQ(1, q_init_calls);
    ASSERT_EQ(65, init_queue_calls);
    ASSERT_EQ(0, init_queue_vt[0]);
    ASSERT_EQ(0, init_queue_id[0]);
    ASSERT_PTR_EQ(&TIME_$RTEQ, init_queue_ptr[0]);
    ASSERT_EQ((int8_t)0xFF, init_queue_vt[1]);
    ASSERT_EQ(1, init_queue_id[1]);
    ASSERT_PTR_EQ(&TIME_$VTQ[0], init_queue_ptr[1]);
    ASSERT_EQ((int8_t)0xFF, init_queue_vt[64]);
    ASSERT_EQ(64, init_queue_id[64]);
    ASSERT_PTR_EQ(&TIME_$VTQ[63], init_queue_ptr[64]);

    ASSERT_EQ(2, di_init_calls);
    ASSERT_PTR_EQ(&TIME_$DI_VT, di_init_elems[0]);
    ASSERT_PTR_EQ(&TIME_$DI_RTE, di_init_elems[1]);

    ASSERT_EQ(1, read_cal_calls);
    ASSERT_EQ(0x11223344, TIME_$CLOCKH);
    ASSERT_EQ(0x5566, TIME_$CLOCKL);
    ASSERT_EQ(0x11223344, TIME_$CURRENT_CLOCKH);
    ASSERT_EQ(0x5566, TIME_$CURRENT_CLOCKL);
    ASSERT_EQ(0x11223344, TIME_$BOOT_TIME);
    ASSERT_EQ(1000 + 0x12CEA600, TIME_$CURRENT_TIME);
    ASSERT_EQ(0, TIME_$CURRENT_USEC);
    ASSERT_EQ(0, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0, TIME_$CURRENT_DELTA);
    ASSERT_EQ(0x1047, TIME_$CURRENT_TICK);
    ASSERT_EQ(1, timer_init_calls);
}

TEST(init_without_calendar)
{
    uint8_t flag = 0x7F;                        /* bit 7 clear */

    reset();
    TIME_$CLOCKH = 0xAAAA;
    TIME_$CURRENT_SKEW = 7;
    TIME_$CURRENT_TICK = 0;

    TIME_$INIT(&flag);

    ASSERT_EQ(0, read_cal_calls);
    ASSERT_EQ(0xAAAA, TIME_$CLOCKH);
    ASSERT_EQ(7, TIME_$CURRENT_SKEW);
    ASSERT_EQ(0x1047, TIME_$CURRENT_TICK);
    ASSERT_EQ(1, timer_init_calls);
    ASSERT_EQ(65, init_queue_calls);
}

int main(void)
{
    printf("test_q_scan_queue:\n");

    RUN_TEST(scan_empty_queue_only_locks);
    RUN_TEST(scan_future_head_rearms_timer);
    RUN_TEST(scan_direct_fires_equal_expiry_under_lock);
    RUN_TEST(scan_fires_all_expired_then_stops);
    RUN_TEST(scan_repeat_reinserts);
    RUN_TEST(scan_unwired_defers_through_dxm);
    RUN_TEST(scan_wired_defers_through_dxm);
    RUN_TEST(scan_both_bits_prefers_unwired);
    RUN_TEST(get_ec_registers_once_and_returns_clock_handle);
    RUN_TEST(get_ec_bad_key);
    RUN_TEST(get_ec_registration_failure);
    RUN_TEST(init_with_calendar);
    RUN_TEST(init_without_calendar);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
