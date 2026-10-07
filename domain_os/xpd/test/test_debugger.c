/*
 * xpd/test/test_debugger.c - the debugger table and event plumbing:
 * XPD_$FIND_DEBUGGER_INDEX, XPD_$REGISTER_DEBUGGER, XPD_$UNREGISTER_DEBUGGER,
 * XPD_$SET_DEBUGGER (debugger.c); XPD_$POST_EVENT, XPD_$CLEANUP (cleanup.c);
 * XPD_$GET_EC, XPD_$GET_EVENT_AND_DATA, XPD_$CONTINUE_PROC, XPD_$SET_ENABLE
 * (events.c; XPD_$CAPTURE_FAULT is only linked here).
 */

#include <stdio.h>
#include <string.h>

#include "xpd/xpd_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-48s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Globals
 * ========================================================================== */

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
MODULE_DATA_DEFINE(proc2_$wired_data_t, PROC2_$WIRED_DATA, 0x00E2B978);
MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);
MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
uid_t UID_$NIL = { 0, 0 };
#include "fim/fim.h"
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);
MODULE_DATA_DEFINE(peb_globals_t, PEB_$INFO, 0x00E24C78);

/* ==========================================================================
 * Mocks
 * ========================================================================== */

static int lock_calls, unlock_calls, lock_held;
void ML_$LOCK(int16_t id)   { (void)id; lock_calls++; lock_held++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlock_calls++; lock_held--; }

/* PROC2_$FIND_ASID: a tiny table of uid -> index */
static uid_t known_uid[8];
static uint16_t known_idx[8];
static status_$t find_asid_status;
static int8_t *find_asid_flag;
uint16_t PROC2_$FIND_ASID(uid_t *proc_uid, int8_t *flag, status_$t *status_ret)
{
    int i;
    find_asid_flag = flag;
    *status_ret = find_asid_status;
    if (find_asid_status != status_$ok) return 0;
    for (i = 0; i < 8; i++) {
        if (known_uid[i].high == proc_uid->high && known_uid[i].low == proc_uid->low)
            return known_idx[i];
    }
    *status_ret = 0x00190001;
    return 0;
}

static int advance_calls;
static ec_$eventcount_t *advance_ec[4];
void EC_$ADVANCE(ec_$eventcount_t *ec) { if (advance_calls < 4) advance_ec[advance_calls] = ec; advance_calls++; }

static int wait_calls;
static ec_$wait_ecs_t wait_ecs;
static ec_$wait_vals_t wait_vals;
static uint16_t wait_sets_response;     /* the "debugger" answers during the wait */
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    wait_calls++;
    wait_ecs = ecs; wait_vals = vals;
    XPD_TARGET(PROC1_$AS_ID)->state = (uint16_t)((XPD_TARGET(PROC1_$AS_ID)->state & ~XPD_STATE_RESPONSE) | wait_sets_response);
    return 0;
}

static status_$t ec2_status;
static ec_$eventcount_t *ec2_ec;
void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret) { ec2_ec = ec1; *status_ret = ec2_status; return (void *)0x5150; }

/* CAPTURE_FAULT's callees: never reached here */
void (FIM_$DELIVER_TRACE_FAULT)(uint32_t as_id_slot) { int16_t as_id = (int16_t)ARCH_PASCAL_SLOT_WORD(as_id_slot); (void)as_id; (void)as_id; }
void (FIM_$CLEAR_TRACE_FAULT)(uint32_t as_id_slot) { int16_t as_id = (int16_t)ARCH_PASCAL_SLOT_WORD(as_id_slot); (void)as_id; (void)as_id; }
void PROC2_$AWAKEN_GUARDIAN(int16_t *proc_index) { (void)proc_index; }
int8_t PROC1_$SUSPEND(uint16_t pid, status_$t *st) { (void)pid; *st = 0; return 0; }
void XPD_$FP_GET_STATE(void *a, void *b) { (void)a; (void)b; }
void XPD_$FP_PUT_STATE(void *a, void *b) { (void)a; (void)b; }

#include "../xpd_data.c"
#include "../debugger.c"
#include "../cleanup.c"
#include "../events.c"

static void reset(void)
{
    memset(XPD_$DATA, 0, sizeof(XPD_$DATA));
    memset(PROC2_$DATA.info, 0, sizeof(PROC2_$DATA.info));
    memset(PROC2_$DATA.pid_to_index, 0, sizeof(PROC2_$DATA.pid_to_index));
    memset(PROC2_$UNWIRED_DATA.uid, 0, sizeof(PROC2_$UNWIRED_DATA.uid));
    memset(known_uid, 0, sizeof(known_uid));
    memset(known_idx, 0, sizeof(known_idx));
    PROC1_$CURRENT = 3;
    PROC1_$AS_ID = 5;
    lock_calls = unlock_calls = lock_held = 0;
    find_asid_status = status_$ok;
    advance_calls = 0;
    wait_calls = 0;
    wait_sets_response = 0;
    ec2_status = status_$ok;
}

/* uid U(n) is process index n */
#define U(n) ((uid_t){ 0x1000u + (n), (n) })
static void know(int i, uid_t u, uint16_t idx) { known_uid[i] = u; known_idx[i] = idx; PROC2_$UNWIRED_DATA.uid[idx] = u; }

/* ==========================================================================
 * The debugger table
 * ========================================================================== */

TEST(find_debugger_index)
{
    status_$t st = 0x55;
    reset();
    XPD_DEBUGGER(3)->asid = 0x21;
    ASSERT_EQ(3, XPD_$FIND_DEBUGGER_INDEX(0x21, &st));
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, XPD_$FIND_DEBUGGER_INDEX(0x22, &st));
    ASSERT_EQ(status_$xpd_not_a_debugger, st);
    /* slot 0 (the +0x478 bytes) is never consulted */
    XPD_DEBUGGER(0)->asid = 0x22;
    ASSERT_EQ(0, XPD_$FIND_DEBUGGER_INDEX(0x22, &st));
}

TEST(register_debugger)
{
    status_$t st = 0x55;
    reset();
    XPD_DEBUGGER(1)->asid = 0x11;
    XPD_DEBUGGER(2)->ec.value = 77;
    /* first free slot is 2; its EC value is zeroed */
    ASSERT_EQ(2, XPD_$REGISTER_DEBUGGER(0x22, &st));
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0x22, XPD_DEBUGGER(2)->asid);
    ASSERT_EQ(0, XPD_DEBUGGER(2)->ec.value);
    ASSERT_EQ(1, lock_calls); ASSERT_EQ(1, unlock_calls);
    /* already registered: that slot, and the error */
    ASSERT_EQ(1, XPD_$REGISTER_DEBUGGER(0x11, &st));
    ASSERT_EQ(status_$xpd_already_a_debugger, st);
    ASSERT_EQ(0, lock_held);
    /* fill up */
    XPD_DEBUGGER(3)->asid = 0x33; XPD_DEBUGGER(4)->asid = 0x44;
    XPD_DEBUGGER(5)->asid = 0x55; XPD_DEBUGGER(6)->asid = 0x66;
    ASSERT_EQ(0, XPD_$REGISTER_DEBUGGER(0x77, &st));
    ASSERT_EQ(status_$xpd_debugger_table_full, st);
    ASSERT_EQ(0, lock_held);
}

TEST(unregister_debugger_detaches_and_continues)
{
    status_$t st = 0x55;
    reset();
    XPD_DEBUGGER(2)->asid = 0x22;
    know(0, U(7), 7);
    /* target 7 attached to slot 2 with an event pending; target 9 attached
     * to slot 1; target 8 attached to slot 2 without an event */
    XPD_TARGET(7)->state = (2 << XPD_STATE_DEBUGGER_SHIFT) | (4 << XPD_STATE_EVENT_SHIFT) | XPD_STATE_ENABLED;
    XPD_TARGET(8)->state = (2 << XPD_STATE_DEBUGGER_SHIFT);
    XPD_TARGET(9)->state = (1 << XPD_STATE_DEBUGGER_SHIFT) | (1 << XPD_STATE_EVENT_SHIFT);
    XPD_$UNREGISTER_DEBUGGER(0x22, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, XPD_DEBUGGER(2)->asid);
    ASSERT_EQ(0, XPD_TARGET(8)->state & XPD_STATE_DEBUGGER);
    ASSERT_EQ(1 << XPD_STATE_DEBUGGER_SHIFT, XPD_TARGET(9)->state & XPD_STATE_DEBUGGER);
    /* target 7 was continued with response 2: event cleared, response 2 */
    ASSERT_EQ(0, XPD_TARGET(7)->state & XPD_STATE_DEBUGGER);
    ASSERT_EQ(0, XPD_TARGET(7)->state & XPD_STATE_EVENT);
    ASSERT_EQ(2 << XPD_STATE_RESPONSE_SHIFT, XPD_TARGET(7)->state & XPD_STATE_RESPONSE);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((long long)(intptr_t)&XPD_TARGET(7)->ec, (long long)(intptr_t)advance_ec[0]);
    ASSERT_EQ(0, lock_held);

    XPD_$UNREGISTER_DEBUGGER(0x99, &st);
    ASSERT_EQ(status_$xpd_not_a_debugger, st);
    ASSERT_EQ(0, lock_held);
}

TEST(set_debugger_registers_when_target_nil)
{
    status_$t st = 0x55;
    uid_t dbg = U(2), nil = { 0, 0 };
    reset();
    know(0, dbg, 0x22);
    XPD_$SET_DEBUGGER(&nil, &nil, &st);
    ASSERT_EQ(status_$ok, st);
    XPD_$SET_DEBUGGER(&dbg, &nil, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0x22, XPD_DEBUGGER(1)->asid);
    ASSERT_EQ((long long)(intptr_t)&xpd_$find_asid_flag, (long long)(intptr_t)find_asid_flag);
    ASSERT_EQ(0, xpd_$find_asid_flag);
}

TEST(set_debugger_attaches)
{
    status_$t st = 0x55;
    uid_t dbg = U(2), tgt = U(7);
    reset();
    know(0, dbg, 0x22); know(1, tgt, 7);
    XPD_DEBUGGER(4)->asid = 0x22;
    XPD_$SET_DEBUGGER(&dbg, &tgt, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(4 << XPD_STATE_DEBUGGER_SHIFT, XPD_TARGET(7)->state & XPD_STATE_DEBUGGER);
    ASSERT_EQ(0, lock_held);
    /* attaching again: the slot is rewritten and illegal_target_setup */
    XPD_DEBUGGER(5)->asid = 0x33;
    know(2, U(3), 0x33);
    XPD_$SET_DEBUGGER(&known_uid[2], &tgt, &st);
    ASSERT_EQ(status_$xpd_illegal_target_setup, st);
    ASSERT_EQ(5 << XPD_STATE_DEBUGGER_SHIFT, XPD_TARGET(7)->state & XPD_STATE_DEBUGGER);
    ASSERT_EQ(0, lock_held);
    /* an unknown debugger */
    { uid_t stranger = { 0xDEAD, 0xBEEF };
      XPD_$SET_DEBUGGER(&stranger, &tgt, &st);
      ASSERT_EQ(status_$xpd_debugger_not_found, st); }
    /* a debugger with no slot */
    know(3, U(4), 0x44);
    XPD_$SET_DEBUGGER(&known_uid[3], &tgt, &st);
    ASSERT_EQ(status_$xpd_not_a_debugger, st);
}

TEST(set_debugger_detaches_when_debugger_nil)
{
    status_$t st = 0x55;
    uid_t nil = { 0, 0 }, tgt = U(7);
    reset();
    know(0, tgt, 7);
    XPD_TARGET(7)->state = (4 << XPD_STATE_DEBUGGER_SHIFT) | (3 << XPD_STATE_EVENT_SHIFT);
    XPD_$SET_DEBUGGER(&nil, &tgt, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, XPD_TARGET(7)->state & XPD_STATE_DEBUGGER);
    ASSERT_EQ(0, XPD_TARGET(7)->state & XPD_STATE_EVENT);     /* continued */
    ASSERT_EQ(2 << XPD_STATE_RESPONSE_SHIFT, XPD_TARGET(7)->state & XPD_STATE_RESPONSE);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ(0, lock_held);
    /* no slot at all: just ok */
    advance_calls = 0;
    XPD_$SET_DEBUGGER(&nil, &tgt, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, advance_calls);
}

TEST(set_debugger_same_process_unregisters)
{
    status_$t st = 0x55;
    uid_t p = U(2);
    reset();
    know(0, p, 0x22);
    XPD_DEBUGGER(3)->asid = 0x22;
    XPD_$SET_DEBUGGER(&p, &p, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, XPD_DEBUGGER(3)->asid);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

TEST(post_event_without_debugger)
{
    xpd_$event_type_t ev = 0x0104;
    status_$t sv = 0x11;
    xpd_$response_t resp = 0x55;
    reset();
    XPD_$POST_EVENT(&ev, &sv, &resp);
    ASSERT_EQ(2, resp);
    XPD_TARGET(5)->state = (1 << XPD_STATE_DEBUGGER_SHIFT);   /* not enabled */
    XPD_$POST_EVENT(&ev, &sv, &resp);
    ASSERT_EQ(2, resp);
    ASSERT_EQ(0, wait_calls);
}

TEST(post_event_round_trip)
{
    xpd_$event_type_t ev = 0x0104;      /* low byte 4 */
    status_$t sv = 0x00160012;
    xpd_$response_t resp = 0x55;
    reset();
    XPD_TARGET(5)->state = (3 << XPD_STATE_DEBUGGER_SHIFT) | XPD_STATE_ENABLED | XPD_STATE_ACKED | (9 << XPD_STATE_EVENT_SHIFT);
    XPD_TARGET(5)->ec.value = 40;
    wait_sets_response = 1 << XPD_STATE_RESPONSE_SHIFT;
    XPD_$POST_EVENT(&ev, &sv, &resp);
    ASSERT_EQ(0, XPD_TARGET(5)->ec.value);
    ASSERT_EQ(4, (XPD_TARGET(5)->state & XPD_STATE_EVENT) >> XPD_STATE_EVENT_SHIFT);
    ASSERT_EQ(0, XPD_TARGET(5)->state & XPD_STATE_ACKED);
    ASSERT_EQ(0x00160012, XPD_TARGET(5)->status);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((long long)(intptr_t)&XPD_DEBUGGER(3)->ec, (long long)(intptr_t)advance_ec[0]);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ((long long)(intptr_t)&XPD_TARGET(5)->ec, (long long)(intptr_t)wait_ecs.ec[0]);
    ASSERT_EQ(0, (long long)(intptr_t)wait_ecs.ec[1]);
    ASSERT_EQ(1, wait_vals.val[0]);
    ASSERT_EQ(0, wait_vals.val[1]);
    ASSERT_EQ(1, resp);
}

TEST(cleanup)
{
    reset();
    PROC1_$AS_ID = 4;
    XPD_DEBUGGER(2)->asid = 4;
    XPD_TARGET(4)->state = 0xF1FF;       /* no slot: the post is a no-op */
    XPD_$CLEANUP();
    ASSERT_EQ(0, XPD_DEBUGGER(2)->asid);
    ASSERT_EQ(0x701F, XPD_TARGET(4)->state);
    ASSERT_EQ(0, wait_calls);
    ASSERT_EQ(0, lock_held);
}

TEST(get_ec)
{
    int16_t key = 0;
    void *ec = NULL;
    status_$t st = 0x55;
    reset();
    XPD_$GET_EC(&key, &ec, &st);
    ASSERT_EQ(status_$xpd_not_a_debugger, st);
    XPD_DEBUGGER(2)->asid = 5;
    key = 1;
    XPD_$GET_EC(&key, &ec, &st);
    ASSERT_EQ(status_$xpd_invalid_ec_key, st);
    key = 0;
    XPD_$GET_EC(&key, &ec, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0x5150, (long long)(intptr_t)ec);
    ASSERT_EQ((long long)(intptr_t)&XPD_DEBUGGER(2)->ec, (long long)(intptr_t)ec2_ec);
    ec2_status = 0x00070004;
    XPD_$GET_EC(&key, &ec, &st);
    ASSERT_EQ((status_$t)0x80070004u, st);
}

TEST(get_event_and_data)
{
    uid_t u = { 9, 9 };
    uint16_t ev = 0x55;
    status_$t st = 0x55;
    reset();
    XPD_DEBUGGER(2)->asid = 5;
    PROC2_$UNWIRED_DATA.uid[7] = U(7);
    /* 6: wrong slot; 7: acked; 8: not enabled; 9: no event; 10: the one */
    XPD_TARGET(6)->state = (1 << XPD_STATE_DEBUGGER_SHIFT) | XPD_STATE_ENABLED | (1 << XPD_STATE_EVENT_SHIFT);
    XPD_TARGET(7)->state = (2 << XPD_STATE_DEBUGGER_SHIFT) | XPD_STATE_ENABLED | XPD_STATE_ACKED | (1 << XPD_STATE_EVENT_SHIFT);
    XPD_TARGET(8)->state = (2 << XPD_STATE_DEBUGGER_SHIFT) | (1 << XPD_STATE_EVENT_SHIFT);
    XPD_TARGET(9)->state = (2 << XPD_STATE_DEBUGGER_SHIFT) | XPD_STATE_ENABLED;
    XPD_TARGET(10)->state = (2 << XPD_STATE_DEBUGGER_SHIFT) | XPD_STATE_ENABLED | (6 << XPD_STATE_EVENT_SHIFT);
    XPD_TARGET(10)->status = 0x00120015;
    PROC2_$UNWIRED_DATA.uid[10] = U(10);
    XPD_$GET_EVENT_AND_DATA(&u, &ev, &st);
    ASSERT_EQ(6, ev);
    ASSERT_EQ(0x00120015, st);
    ASSERT_EQ(10, u.low);
    ASSERT_EQ(XPD_STATE_ACKED, XPD_TARGET(10)->state & XPD_STATE_ACKED);
    /* now nothing is left */
    XPD_$GET_EVENT_AND_DATA(&u, &ev, &st);
    ASSERT_EQ(0, ev);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, u.high); ASSERT_EQ(0, u.low);
}

TEST(continue_proc)
{
    status_$t st = 0x55;
    xpd_$response_t resp = 0x0103;
    reset();
    know(0, U(7), 7);
    XPD_$CONTINUE_PROC(&known_uid[0], &resp, &st);
    ASSERT_EQ(status_$xpd_target_not_suspended, st);
    XPD_TARGET(7)->state = (5 << XPD_STATE_EVENT_SHIFT) | (2 << XPD_STATE_RESPONSE_SHIFT) | XPD_STATE_ENABLED;
    st = 0x55;
    XPD_$CONTINUE_PROC(&known_uid[0], &resp, &st);
    ASSERT_EQ(status_$ok, st);              /* FIND_ASID's status stands */
    ASSERT_EQ(0, XPD_TARGET(7)->state & XPD_STATE_EVENT);
    ASSERT_EQ(3 << XPD_STATE_RESPONSE_SHIFT, XPD_TARGET(7)->state & XPD_STATE_RESPONSE);
    ASSERT_EQ(XPD_STATE_ENABLED, XPD_TARGET(7)->state & XPD_STATE_ENABLED);
    ASSERT_EQ(1, advance_calls);
    /* an unknown process: FIND_ASID's status */
    { uid_t stranger = { 0xDEAD, 0xBEEF };
      XPD_$CONTINUE_PROC(&stranger, &resp, &st);
      ASSERT_EQ(0x00190001, st); }
}

TEST(set_enable)
{
    status_$t st = 0x55;
    int8_t on = -1, off = 0x7F;
    reset();
    know(0, U(7), 7);
    XPD_TARGET(7)->state = (3 << XPD_STATE_EVENT_SHIFT) | (1 << XPD_STATE_DEBUGGER_SHIFT);
    XPD_$SET_ENABLE(&known_uid[0], &on, &st);
    ASSERT_EQ(XPD_STATE_ENABLED, XPD_TARGET(7)->state & XPD_STATE_ENABLED);
    ASSERT_EQ(0, XPD_TARGET(7)->state & XPD_STATE_EVENT);
    ASSERT_EQ(1 << XPD_STATE_DEBUGGER_SHIFT, XPD_TARGET(7)->state & XPD_STATE_DEBUGGER);
    ASSERT_EQ(0, advance_calls);
    ASSERT_EQ(0, lock_held);
    /* disabling with an event pending continues it */
    XPD_TARGET(7)->state |= (3 << XPD_STATE_EVENT_SHIFT);
    XPD_$SET_ENABLE(&known_uid[0], &off, &st);
    ASSERT_EQ(0, XPD_TARGET(7)->state & XPD_STATE_ENABLED);
    ASSERT_EQ(0, XPD_TARGET(7)->state & XPD_STATE_EVENT);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ(0, lock_held);
    /* unknown: lock/unlock still bracket it */
    lock_calls = 0;
    { uid_t stranger = { 0xDEAD, 0xBEEF };
      XPD_$SET_ENABLE(&stranger, &on, &st);
      ASSERT_EQ(0x00190001, st); }
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0, lock_held);
}

int main(void)
{
    printf("XPD debugger-table / event tests\n");
    RUN_TEST(find_debugger_index);
    RUN_TEST(register_debugger);
    RUN_TEST(unregister_debugger_detaches_and_continues);
    RUN_TEST(set_debugger_registers_when_target_nil);
    RUN_TEST(set_debugger_attaches);
    RUN_TEST(set_debugger_detaches_when_debugger_nil);
    RUN_TEST(set_debugger_same_process_unregisters);
    RUN_TEST(post_event_without_debugger);
    RUN_TEST(post_event_round_trip);
    RUN_TEST(cleanup);
    RUN_TEST(get_ec);
    RUN_TEST(get_event_and_data);
    RUN_TEST(continue_proc);
    RUN_TEST(set_enable);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
