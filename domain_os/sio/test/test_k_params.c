/*
 * sio/test/test_k_params.c - the SVC-level SIO routines
 *
 *   SIO_$K_SET_PARAM    0x00E680AC    SIO_$K_TIMED_BREAK  0x00E67EE0
 *   SIO_$K_SIGNAL_WAIT  0x00E67FBE
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int __host_intr_disable_count = 0;

#include "sio/sio_internal.h"

uint16_t PROC1_$AS_ID;
ec_$eventcount_t FIM_$QUIT_EC[4];
uint32_t FIM_$QUIT_VALUE[4];

static sio_desc_t desc;
static status_$t gd_status;
sio_desc_t *SIO_$I_GET_DESC(int16_t line_num, status_$t *status_ret)
{ (void)line_num; *status_ret = gd_status; return &desc; }

static int setp_calls; static sio_params_t setp_copy; static uint32_t setp_mask; static status_$t setp_status;
static void mock_set_params(m68k_ptr_t ctx, sio_params_t *p, uint32_t mask, status_$t *st)
{ (void)ctx; setp_calls++; memcpy(&setp_copy, p, sizeof setp_copy); setp_mask = mask; *st = setp_status; }

static int inq_calls; static uint32_t inq_mask; static sio_params_t *inq_p; static uint32_t inq_flags1_set;
static status_$t inq_status;
static void mock_inq_params(m68k_ptr_t ctx, sio_params_t *p, uint32_t mask, status_$t *st)
{ (void)ctx; inq_calls++; inq_mask = mask; inq_p = p; p->flags1 |= inq_flags1_set; *st = inq_status; }

static int waitn_calls; static uint16_t waitn_ret; static int32_t waitn_vals[2]; static ec_$eventcount_t *waitn_ecs[2];
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{ waitn_calls++; (void)num_ecs; waitn_ecs[0] = ecs[0]; waitn_ecs[1] = ecs[1];
  waitn_vals[0] = wait_val[0]; waitn_vals[1] = wait_val[1]; return waitn_ret; }

static int sb_calls; static uint8_t sb_enable[2];
void sio_$set_break(sio_desc_t *d, int8_t enable) { (void)d; if (sb_calls < 2) sb_enable[sb_calls] = (uint8_t)enable; sb_calls++; }

static int w2_calls; static uint16_t w2_type; static clock_t w2_delay; static void *w2_ec; static uint32_t w2_count; static int8_t w2_ret;
int8_t TIME_$WAIT2(uint16_t *delay_type, clock_t *delay, void *extra_ec, uint32_t *count, status_$t *status)
{ w2_calls++; w2_type = *delay_type; w2_delay = *delay; w2_ec = extra_ec; w2_count = *count; (void)status; return w2_ret; }

#include "../k_set_param.c"
#include "../k_signal_wait.c"
#include "../k_timed_break.c"

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

static m68k_ptr_t fn_va(void (*f)(void)) { return (m68k_ptr_t)((uintptr_t)f - ARCH_HOST_VA_BASE); }

static void reset(void)
{
    memset(&desc, 0, sizeof desc);
    ARCH_HOST_VA_BASE = (uintptr_t)mock_set_params & ~(uintptr_t)0xFFFFFFFFu;
    desc.set_params = fn_va((void (*)(void))mock_set_params);
    desc.inq_params = fn_va((void (*)(void))mock_inq_params);
    gd_status = 0; setp_calls = 0; setp_status = 0; inq_calls = 0; inq_status = 0; inq_flags1_set = 0;
    waitn_calls = 0; waitn_ret = 1; sb_calls = 0; w2_calls = 0; w2_ret = 0;
    PROC1_$AS_ID = 2;
    memset(FIM_$QUIT_EC, 0, sizeof FIM_$QUIT_EC); memset(FIM_$QUIT_VALUE, 0, sizeof FIM_$QUIT_VALUE);
}

/* ---- SIO_$K_SET_PARAM ----------------------------------------------------- */

TEST(set_param_unchanged_selectors_are_dropped)
{
    int16_t line = 1; uint32_t mask = 0x7E7F; status_$t st = -1; sio_params_t p;
    reset();
    desc.params.flags1 = 0x09; desc.params.flags2 = 0x4F; desc.params.break_mask = 0x3F;
    desc.params.baud_rate = 0x000E000E; desc.params.char_size = 3; desc.params.stop_bits = 1; desc.params.parity = 0;
    memcpy(&p, &desc.params, sizeof p);
    SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, setp_calls);                       /* every handled selector dropped */
    /* bits 7 and 8 have no arm: they reach the driver as they are */
    mask = 0x7FFF;
    SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(1, setp_calls);
    ASSERT_EQ(0x0180, setp_mask);
}

TEST(set_param_merges_only_selected_bits)
{
    int16_t line = 1; uint32_t mask = 0x0020 | 0x0800; status_$t st = -1; sio_params_t p;
    reset();
    desc.params.flags1 = 0x08; desc.params.flags2 = 0x00;
    memset(&p, 0xFF, sizeof p);                     /* every bit set on the caller's side */
    SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(1, setp_calls);
    ASSERT_EQ(0x0820, setp_mask);
    ASSERT_EQ(0x09, setp_copy.flags1);              /* bit 0 merged, bit 3 kept */
    ASSERT_EQ(0x04, setp_copy.flags2);              /* only bit 2 merged */
    ASSERT_EQ(0x09, desc.params.flags1);            /* written back on success */
    ASSERT_EQ(0x04, desc.params.flags2);
}

TEST(set_param_driver_failure_leaves_descriptor)
{
    int16_t line = 1; uint32_t mask = 0x0040; status_$t st = -1; sio_params_t p;
    reset();
    memset(&p, 0, sizeof p); p.flags1 = 0x08;
    setp_status = 0x360001;
    SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0x360001, st);
    ASSERT_EQ(0, desc.params.flags1);
}

TEST(set_param_validation)
{
    int16_t line = 1; uint32_t mask; status_$t st; sio_params_t p;
    reset();
    memset(&p, 0, sizeof p);
    /* speed: either half above 0x10 */
    mask = 0x0001; st = 0; p.baud_rate = 0x00110000; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0x360002, st);
    mask = 0x0002; st = 0; p.baud_rate = 0x00000011; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0x360002, st);
    mask = 0x0003; st = 0; p.baud_rate = 0x00100010; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0, st); ASSERT_EQ(0x00100010, desc.params.baud_rate);
    /* bit 2 is parity (+0x14), bit 4 is char_size (+0x10) */
    mask = 0x0004; st = 0; p.parity = 4; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0x360002, st);
    mask = 0x0004; st = 0; p.parity = 3; p.char_size = 9; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0, st); ASSERT_EQ(3, desc.params.parity); ASSERT_EQ(0, desc.params.char_size);
    mask = 0x0010; st = 0; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0x360002, st);
    mask = 0x0010; st = 0; p.char_size = 2; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0, st); ASSERT_EQ(2, desc.params.char_size);
    /* stop bits 1..3 */
    mask = 0x0008; st = 0; p.stop_bits = 0; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0x360002, st);
    mask = 0x0008; st = 0; p.stop_bits = 4; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0x360002, st);
    mask = 0x0008; st = 0; p.stop_bits = 3; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0, st); ASSERT_EQ(3, desc.params.stop_bits);
    /* break_mask: bits 6..31 must be clear (not just 0xC0) */
    mask = 0x2000; st = 0; p.break_mask = 0x100; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0x360002, st);
    mask = 0x2000; st = 0; p.break_mask = 0x3F; SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0, st); ASSERT_EQ(0x3F, desc.params.break_mask);
}

TEST(set_param_lookup_failure)
{
    int16_t line = 1; uint32_t mask = 0x3FFF; status_$t st = 0; sio_params_t p;
    reset(); gd_status = 0xB000D;
    memset(&p, 0, sizeof p);
    SIO_$K_SET_PARAM(&line, &p, &mask, &st);
    ASSERT_EQ(0xB000D, st);
    ASSERT_EQ(0, setp_calls);
}

/* ---- SIO_$K_SIGNAL_WAIT --------------------------------------------------- */

TEST(signal_wait_returns_when_signal_up)
{
    int16_t line = 3; uint32_t signals = 0x02; status_$t st = -1;
    reset();
    inq_flags1_set = 0x02;
    SIO_$K_SIGNAL_WAIT(&line, &signals, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, inq_calls);
    ASSERT_EQ(0x180, inq_mask);
    ASSERT_PTR_EQ(&desc.params, inq_p);             /* the DESCRIPTOR's block */
    ASSERT_EQ(0, waitn_calls);
}

TEST(signal_wait_quit_resyncs_quit_value)
{
    int16_t line = 3; uint32_t signals = 0x04; status_$t st = -1;
    reset();
    desc.ec.value = 10; FIM_$QUIT_VALUE[2] = 41; FIM_$QUIT_EC[2].value = 42;
    waitn_ret = 2;
    SIO_$K_SIGNAL_WAIT(&line, &signals, &st);
    ASSERT_EQ(1, waitn_calls);
    ASSERT_PTR_EQ(&desc.ec, waitn_ecs[0]);
    ASSERT_PTR_EQ(&FIM_$QUIT_EC[2], waitn_ecs[1]);
    ASSERT_EQ(11, waitn_vals[0]);
    ASSERT_EQ(42, waitn_vals[1]);
    ASSERT_EQ(0x36000a, st);
    ASSERT_EQ(42, FIM_$QUIT_VALUE[2]);
}

TEST(signal_wait_driver_error_returns)
{
    int16_t line = 3; uint32_t signals = 0x04; status_$t st = -1;
    reset(); inq_status = 0x360001;
    SIO_$K_SIGNAL_WAIT(&line, &signals, &st);
    ASSERT_EQ(0x360001, st);
    ASSERT_EQ(0, waitn_calls);
}

/* ---- SIO_$K_TIMED_BREAK --------------------------------------------------- */

TEST(timed_break_delay_and_drop)
{
    int16_t line = 1; uint16_t ms = 1000; status_$t st = -1;
    reset();
    FIM_$QUIT_VALUE[2] = 7;
    SIO_$K_TIMED_BREAK(&line, &ms, &st);
    ASSERT_EQ(2, sb_calls);
    ASSERT_EQ(0xFF, sb_enable[0]);
    ASSERT_EQ(0, sb_enable[1]);
    ASSERT_EQ(1, w2_calls);
    ASSERT_EQ(0, w2_type);
    /* 1000 * 250 = 250000 = 0x3D090: high 3, low 0xD090 */
    ASSERT_EQ(3, w2_delay.high);
    ASSERT_EQ(0xD090, w2_delay.low);
    ASSERT_PTR_EQ(&FIM_$QUIT_EC[2], w2_ec);
    ASSERT_EQ(8, w2_count);
    ASSERT_EQ(0, st);
}

TEST(timed_break_quit)
{
    int16_t line = 1; uint16_t ms = 10; status_$t st = -1;
    reset();
    FIM_$QUIT_VALUE[2] = 7; FIM_$QUIT_EC[2].value = 9;
    w2_ret = -1;
    SIO_$K_TIMED_BREAK(&line, &ms, &st);
    ASSERT_EQ(0xB0006, st);
    ASSERT_EQ(9, FIM_$QUIT_VALUE[2]);
    ASSERT_EQ(2, sb_calls);                          /* break still dropped */
}

int main(void)
{
    printf("SIO K-level tests\n");
    RUN_TEST(set_param_unchanged_selectors_are_dropped);
    RUN_TEST(set_param_merges_only_selected_bits);
    RUN_TEST(set_param_driver_failure_leaves_descriptor);
    RUN_TEST(set_param_validation);
    RUN_TEST(set_param_lookup_failure);
    RUN_TEST(signal_wait_returns_when_signal_up);
    RUN_TEST(signal_wait_quit_resyncs_quit_value);
    RUN_TEST(signal_wait_driver_error_returns);
    RUN_TEST(timed_break_delay_and_drop);
    RUN_TEST(timed_break_quit);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
