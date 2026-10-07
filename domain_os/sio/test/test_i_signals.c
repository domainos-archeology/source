/*
 * sio/test/test_i_signals.c - the interrupt-level SIO entry points
 *
 *   SIO_$I_CTS_CHANGE   0x00E1C6DA    SIO_$I_INHIBIT_RCV   0x00E1C94A
 *   SIO_$I_DCD_CHANGE   0x00E1C73E    SIO_$I_INHIBIT_XMIT  0x00E1C9CE
 *   SIO_$I_ERR          0x00E67D9C    SIO_$I_INIT          0x00E67E5E
 *   SIO_$I_GET_DESC     0x00E667C6
 *
 * Handler cells are 32-bit VAs: the tests point ARCH_HOST_VA_BASE at the
 * high half of the mock functions' addresses so the low 32 bits round-trip.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int __host_intr_disable_count = 0;

#include "sio/sio_internal.h"

/* ---- host storage and mocks --------------------------------------------- */

uint32_t     SIO_$SPIN_LOCK;
term_data_t  TERM_$DATA;

static int tstart_calls; static sio_desc_t *tstart_arg;
void SIO_$I_TSTART(sio_desc_t *desc) { tstart_calls++; tstart_arg = desc; }

static int advance_calls; static ec_$eventcount_t *advance_arg;
void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec) { advance_calls++; advance_arg = ec; }
static int ec_init_calls; static ec_$eventcount_t *ec_init_arg;
void EC_$INIT(ec_$eventcount_t *ec) { ec_init_calls++; ec_init_arg = ec; }

static int lock_calls, unlock_calls; static void *lock_arg; static ml_$spin_token_t unlock_token;
ml_$spin_token_t ML_$SPIN_LOCK(void *lock) { lock_calls++; lock_arg = lock; return 0x4321; }
void (ML_$SPIN_UNLOCK)(void *lock, uint32_t token_slot) { ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token; unlock_calls++; (void)lock; unlock_token = token; }

static short real_line_ret; static status_$t real_line_status; static short real_line_arg;
short TERM_$GET_REAL_LINE(short line_num, status_$t *status_ret)
{ real_line_arg = line_num; *status_ret = real_line_status; return real_line_ret; }

static int data_rcv_calls; static m68k_ptr_t data_rcv_owner; static uint16_t data_rcv_zero;
static int16_t mock_data_rcv(m68k_ptr_t owner, uint16_t zero)
{ data_rcv_calls++; data_rcv_owner = owner; data_rcv_zero = zero; return 7; }

static int dcd_calls; static m68k_ptr_t dcd_owner;
static void mock_dcd_handler(m68k_ptr_t owner) { dcd_calls++; dcd_owner = owner; }

static int setp_calls; static m68k_ptr_t setp_ctx; static sio_params_t *setp_params; static uint32_t setp_mask;
static void mock_set_params(m68k_ptr_t ctx, sio_params_t *p, uint32_t mask, status_$t *st)
{ setp_calls++; setp_ctx = ctx; setp_params = p; setp_mask = mask; *st = 0; }

#include "../i_cts_change.c"
#include "../i_dcd_change.c"
#include "../i_err.c"
#include "../i_get_desc.c"
#include "../i_inhibit_rcv.c"
#include "../i_inhibit_xmit.c"
#include "../i_init.c"

static int tests_passed = 0;
static int tests_failed = 0;
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { printf("  Running %-44s ", #name); test_##name(); \
    tests_passed++; printf("PASSED\n"); } while (0)
#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
        _e, _a, __LINE__); tests_failed++; return; } } while (0)
#define ASSERT_PTR_EQ(expected, actual) do { \
    const void *_e = (const void *)(expected); const void *_a = (const void *)(actual); \
    if (_e != _a) { printf("FAILED\n    Expected: %p, Got: %p at line %d\n", _e, _a, __LINE__); \
        tests_failed++; return; } } while (0)

static sio_desc_t desc;

static m68k_ptr_t fn_va(void (*f)(void))
{
    return (m68k_ptr_t)((uintptr_t)f - ARCH_HOST_VA_BASE);
}

static void reset(void)
{
    memset(&desc, 0, sizeof desc);
    desc.owner = 0x0BADF00D;
    desc.context = 0xC0DE;
    ARCH_HOST_VA_BASE = (uintptr_t)mock_data_rcv & ~(uintptr_t)0xFFFFFFFFu;
    tstart_calls = advance_calls = ec_init_calls = 0;
    lock_calls = unlock_calls = 0;
    data_rcv_calls = dcd_calls = setp_calls = 0;
    real_line_status = 0; real_line_ret = 0;
}

/* ---- SIO_$I_CTS_CHANGE ---------------------------------------------------- */

TEST(cts_asserted_unblocks_and_restarts)
{
    reset();
    desc.state = SIO_XMIT_CTS_BLOCKED | 0x0100;
    SIO_$I_CTS_CHANGE(&desc, -1);
    ASSERT_EQ(0x0100, desc.state);
    ASSERT_EQ(1, tstart_calls);
    ASSERT_PTR_EQ(&desc, tstart_arg);
    ASSERT_EQ(0, desc.pending_int);
    ASSERT_EQ(1, advance_calls);
    ASSERT_PTR_EQ(&desc.ec, advance_arg);
}

TEST(cts_dropped_blocks_only_with_cts_flow)
{
    reset();
    SIO_$I_CTS_CHANGE(&desc, 0);
    ASSERT_EQ(0, desc.state);
    ASSERT_EQ(0, tstart_calls);
    desc.params.flags2 = SIO_CTRL_CTS_FLOW;
    SIO_$I_CTS_CHANGE(&desc, 0);
    ASSERT_EQ(SIO_XMIT_CTS_BLOCKED, desc.state);
    ASSERT_EQ(0, tstart_calls);
    ASSERT_EQ(2, advance_calls);
}

TEST(cts_notify_sets_pending_and_calls_data_rcv)
{
    reset();
    desc.params.break_mask = SIO_INT_CTS_CHANGE;
    SIO_$I_CTS_CHANGE(&desc, 0);
    ASSERT_EQ(SIO_PEND_CTS_CHANGED, desc.pending_int);
    ASSERT_EQ(0, data_rcv_calls);            /* data_rcv cell is 0 */
    desc.data_rcv = fn_va((void (*)(void))mock_data_rcv);
    SIO_$I_CTS_CHANGE(&desc, -1);
    ASSERT_EQ(1, data_rcv_calls);
    ASSERT_EQ(0x0BADF00D, data_rcv_owner);
    ASSERT_EQ(0, data_rcv_zero);
}

/* ---- SIO_$I_DCD_CHANGE ---------------------------------------------------- */

TEST(dcd_asserted_restarts)
{
    reset();
    SIO_$I_DCD_CHANGE(&desc, -1);
    ASSERT_EQ(1, tstart_calls);
    ASSERT_EQ(0, dcd_calls);
    ASSERT_EQ(1, advance_calls);
}

TEST(dcd_lost_with_hangup_calls_handler)
{
    reset();
    desc.params.flags2 = SIO_CTRL_DCD_HANGUP;
    SIO_$I_DCD_CHANGE(&desc, 0);
    ASSERT_EQ(0, dcd_calls);                 /* handler cell is 0 */
    ASSERT_EQ(0, tstart_calls);
    desc.dcd_handler = fn_va((void (*)(void))mock_dcd_handler);
    SIO_$I_DCD_CHANGE(&desc, 0);
    ASSERT_EQ(1, dcd_calls);
    ASSERT_EQ(0x0BADF00D, dcd_owner);
    ASSERT_EQ(0, tstart_calls);
}

TEST(dcd_lost_without_hangup_does_nothing)
{
    reset();
    desc.params.break_mask = SIO_INT_DCD_CHANGE;
    desc.data_rcv = fn_va((void (*)(void))mock_data_rcv);
    SIO_$I_DCD_CHANGE(&desc, 0);
    ASSERT_EQ(0, tstart_calls);
    ASSERT_EQ(0, dcd_calls);
    ASSERT_EQ(SIO_PEND_DCD_CHANGED, desc.pending_int);
    ASSERT_EQ(1, data_rcv_calls);
    ASSERT_EQ(1, advance_calls);
}

/* ---- SIO_$I_ERR ----------------------------------------------------------- */

TEST(err_nothing_pending_returns_ok_without_lock)
{
    reset();
    ASSERT_EQ(0, SIO_$I_ERR(&desc, -1));
    ASSERT_EQ(0, lock_calls);
}

TEST(err_priority_and_clearing)
{
    reset();
    desc.pending_int = 0x1F;
    ASSERT_EQ(0x00360004, SIO_$I_ERR(&desc, -1));    /* framing first */
    ASSERT_EQ(0, desc.pending_int);                    /* all five taken at once */
    ASSERT_EQ(1, lock_calls);
    ASSERT_PTR_EQ(&SIO_$SPIN_LOCK, lock_arg);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x4321, unlock_token);

    desc.pending_int = SIO_PEND_PARITY | SIO_PEND_OVERRUN;
    ASSERT_EQ(0x00360005, SIO_$I_ERR(&desc, -1));
    desc.pending_int = SIO_PEND_OVERRUN | SIO_PEND_DCD_CHANGED;
    ASSERT_EQ(0x00360009, SIO_$I_ERR(&desc, -1));
    desc.pending_int = SIO_PEND_DCD_CHANGED | SIO_PEND_CTS_CHANGED;
    ASSERT_EQ(0x00360006, SIO_$I_ERR(&desc, -1));
    desc.pending_int = SIO_PEND_CTS_CHANGED;
    ASSERT_EQ(0x00360007, SIO_$I_ERR(&desc, -1));
}

TEST(err_signals_only_mask)
{
    reset();
    desc.pending_int = SIO_PEND_FRAMING | SIO_PEND_CTS_CHANGED | 0x20 | 0x100;
    ASSERT_EQ(0x00360007, SIO_$I_ERR(&desc, 0));      /* 0x18 mask skips framing */
    ASSERT_EQ(SIO_PEND_FRAMING | 0x20 | 0x100, desc.pending_int);
    desc.pending_int = 0x20 | 0x100;                   /* nothing under either mask */
    ASSERT_EQ(0, SIO_$I_ERR(&desc, -1));
    ASSERT_EQ(0x20 | 0x100, desc.pending_int);
}

/* ---- SIO_$I_GET_DESC ------------------------------------------------------ */

TEST(get_desc_returns_dtte_tty_handler)
{
    sio_desc_t *r;
    reset();
    memset(&TERM_$DATA, 0, sizeof TERM_$DATA);
    ARCH_HOST_VA_BASE = (uintptr_t)&desc & ~(uintptr_t)0xFFFFFFFFu;
    TERM_$DATA.dtte[2].tty_handler = (m68k_ptr_t)((uintptr_t)&desc - ARCH_HOST_VA_BASE);
    real_line_ret = 2;
    {
        status_$t st = 0x55;
        r = SIO_$I_GET_DESC(9, &st);
        ASSERT_EQ(9, real_line_arg);
        ASSERT_EQ(0, st);
        ASSERT_PTR_EQ(&desc, r);
    }
}

TEST(get_desc_failure_paths)
{
    status_$t st;
    reset();
    memset(&TERM_$DATA, 0, sizeof TERM_$DATA);
    real_line_ret = 1;
    st = 0;
    ASSERT_PTR_EQ(NULL, SIO_$I_GET_DESC(1, &st));
    ASSERT_EQ(status_$requested_line_or_operation_not_implemented, st);
    real_line_status = 0x77;
    ASSERT_PTR_EQ(NULL, SIO_$I_GET_DESC(1, &st));
    ASSERT_EQ(0x77, st);                     /* left as TERM_$GET_REAL_LINE set it */
}

/* ---- SIO_$I_INHIBIT_RCV --------------------------------------------------- */

TEST(inhibit_rcv_soft_flow_calls_set_params)
{
    reset();
    desc.params.flags2 = SIO_CTRL_SOFT_FLOW;
    desc.params.flags1 = 0xF1;
    desc.set_params = fn_va((void (*)(void))mock_set_params);
    SIO_$I_INHIBIT_RCV(&desc, -1, 0);
    ASSERT_EQ(0xF0, desc.params.flags1);
    ASSERT_EQ(1, setp_calls);
    ASSERT_EQ(0xC0DE, setp_ctx);
    ASSERT_PTR_EQ(&desc.params, setp_params);
    ASSERT_EQ(0x20, setp_mask);
    ASSERT_EQ(0, tstart_calls);
    SIO_$I_INHIBIT_RCV(&desc, 0, 0);
    ASSERT_EQ(0xF1, desc.params.flags1);
    ASSERT_EQ(2, setp_calls);
}

TEST(inhibit_rcv_without_soft_flow_skips_set_params)
{
    reset();
    desc.set_params = fn_va((void (*)(void))mock_set_params);
    SIO_$I_INHIBIT_RCV(&desc, -1, 0);
    ASSERT_EQ(0, setp_calls);
    ASSERT_EQ(0, tstart_calls);
}

TEST(inhibit_rcv_update_xmit_state_machine)
{
    reset();
    /* inhibit, DEFER_COMPLETE clear: drop DEFER_INHIBIT, raise DEFER_PENDING */
    desc.state = SIO_XMIT_DEFER_INHIBIT;
    SIO_$I_INHIBIT_RCV(&desc, -1, -1);
    ASSERT_EQ(SIO_XMIT_DEFER_PENDING, desc.state);
    ASSERT_EQ(1, tstart_calls);
    /* inhibit, DEFER_COMPLETE set: only drop DEFER_INHIBIT */
    desc.state = SIO_XMIT_DEFER_INHIBIT | SIO_XMIT_DEFER_COMPLETE;
    SIO_$I_INHIBIT_RCV(&desc, -1, -1);
    ASSERT_EQ(SIO_XMIT_DEFER_COMPLETE, desc.state);
    /* release, DEFER_COMPLETE set: raise DEFER_INHIBIT, drop DEFER_PENDING */
    desc.state = SIO_XMIT_DEFER_COMPLETE | SIO_XMIT_DEFER_PENDING;
    SIO_$I_INHIBIT_RCV(&desc, 0, -1);
    ASSERT_EQ(SIO_XMIT_DEFER_COMPLETE | SIO_XMIT_DEFER_INHIBIT, desc.state);
    /* release, DEFER_COMPLETE clear: only drop DEFER_PENDING */
    desc.state = SIO_XMIT_DEFER_PENDING | 0x0100;
    SIO_$I_INHIBIT_RCV(&desc, 0, -1);
    ASSERT_EQ(0x0100, desc.state);
    ASSERT_EQ(4, tstart_calls);
}

/* ---- SIO_$I_INHIBIT_XMIT -------------------------------------------------- */

TEST(inhibit_xmit_sets_or_clears_and_restarts)
{
    reset();
    SIO_$I_INHIBIT_XMIT(&desc, -1);
    ASSERT_EQ(SIO_XMIT_INHIBITED, desc.state);
    ASSERT_EQ(0, tstart_calls);
    SIO_$I_INHIBIT_XMIT(&desc, 0);
    ASSERT_EQ(0, desc.state);
    ASSERT_EQ(1, tstart_calls);
}

/* ---- SIO_$I_INIT ---------------------------------------------------------- */

TEST(init_clears_three_fields_and_inits_ec)
{
    reset();
    desc.state = 0xFFFF; desc.pending_int = 0xFFFFFFFF; desc.params.flags2 = 0xFFFFFFFF;
    desc.params.flags1 = 0x11; desc.params.break_mask = 0x22;
    SIO_$I_INIT(&desc);
    ASSERT_EQ(0, desc.state);
    ASSERT_EQ(0, desc.pending_int);
    ASSERT_EQ(0, desc.params.flags2);
    ASSERT_EQ(0x11, desc.params.flags1);
    ASSERT_EQ(0x22, desc.params.break_mask);
    ASSERT_EQ(1, ec_init_calls);
    ASSERT_PTR_EQ(&desc.ec, ec_init_arg);
}

int main(void)
{
    printf("SIO interrupt-level entry point tests\n");
    RUN_TEST(cts_asserted_unblocks_and_restarts);
    RUN_TEST(cts_dropped_blocks_only_with_cts_flow);
    RUN_TEST(cts_notify_sets_pending_and_calls_data_rcv);
    RUN_TEST(dcd_asserted_restarts);
    RUN_TEST(dcd_lost_with_hangup_calls_handler);
    RUN_TEST(dcd_lost_without_hangup_does_nothing);
    RUN_TEST(err_nothing_pending_returns_ok_without_lock);
    RUN_TEST(err_priority_and_clearing);
    RUN_TEST(err_signals_only_mask);
    RUN_TEST(get_desc_returns_dtte_tty_handler);
    RUN_TEST(get_desc_failure_paths);
    RUN_TEST(inhibit_rcv_soft_flow_calls_set_params);
    RUN_TEST(inhibit_rcv_without_soft_flow_skips_set_params);
    RUN_TEST(inhibit_rcv_update_xmit_state_machine);
    RUN_TEST(inhibit_xmit_sets_or_clears_and_restarts);
    RUN_TEST(init_clears_three_fields_and_inits_ec);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
