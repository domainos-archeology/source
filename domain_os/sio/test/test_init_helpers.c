/*
 * sio/test/test_init_helpers.c - the small SIO routines around SIO_$INIT
 *
 *   SIO_$INIT_DESC           0x00E32AB2    SIO_$K_INQ_PARAM   0x00E6832A
 *   SIO_$INIT_DRAIN_HANDLER  0x00E32BB8    SIO_$I_RCV         0x00E1C620
 *   SIO_$INIT_DTTE           0x00E32B76    SIO_$I_XMIT_DONE   0x00E1C6B4
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int __host_intr_disable_count = 0;

#include "sio/sio_internal.h"

static int ec_init_calls; static void *ec_init_args[4];
void EC_$INIT(ec_$eventcount_t *ec) { if (ec_init_calls < 4) ec_init_args[ec_init_calls] = ec; ec_init_calls++; }
static int i_init_calls; static sio_desc_t *i_init_arg;
void SIO_$I_INIT(sio_desc_t *desc) { i_init_calls++; i_init_arg = desc; }
void TTY_$I_OUTPUT_BUFFER_DRAINED(tty_desc_t *tty) { (void)tty; }
static int tstart_calls; static uint16_t tstart_sets;
void SIO_$I_TSTART(sio_desc_t *desc) { tstart_calls++; desc->state |= tstart_sets; }
static sio_desc_t *gd_ret; static status_$t gd_status; static int16_t gd_line;
sio_desc_t *SIO_$I_GET_DESC(int16_t line_num, status_$t *status_ret)
{ gd_line = line_num; *status_ret = gd_status; return gd_ret; }

static int iq_calls; static m68k_ptr_t iq_ctx; static sio_params_t *iq_p; static uint32_t iq_mask;
static void mock_inq_params(m68k_ptr_t ctx, sio_params_t *p, uint32_t mask, status_$t *st)
{ iq_calls++; iq_ctx = ctx; iq_p = p; iq_mask = mask; *st = 0x1234; }
static int rcv_calls; static m68k_ptr_t rcv_owner; static uint8_t rcv_ch;
static int16_t mock_rcv(m68k_ptr_t owner, uint8_t ch) { rcv_calls++; rcv_owner = owner; rcv_ch = ch; return 0; }
static int drcv_calls; static uint8_t drcv_ch;
static int16_t mock_data_rcv(m68k_ptr_t owner, uint8_t ch) { (void)owner; drcv_calls++; drcv_ch = ch; return 0; }
static int srcv_calls; static m68k_ptr_t srcv_owner;
static void mock_special_rcv(m68k_ptr_t owner) { srcv_calls++; srcv_owner = owner; }

#include "../init_desc.c"
#include "../init_drain_handler.c"
#include "../init_dtte.c"
#include "../k_inq_param.c"
#include "../i_rcv.c"
#include "../i_xmit_done.c"

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
static m68k_ptr_t fn_va(void (*f)(void)) { return (m68k_ptr_t)((uintptr_t)f - ARCH_HOST_VA_BASE); }

static void reset(void)
{
    memset(&desc, 0, sizeof desc);
    ARCH_HOST_VA_BASE = (uintptr_t)mock_rcv & ~(uintptr_t)0xFFFFFFFFu;
    ec_init_calls = i_init_calls = tstart_calls = iq_calls = rcv_calls = drcv_calls = srcv_calls = 0;
    tstart_sets = 0; gd_status = 0; gd_ret = &desc;
}

TEST(init_desc_copies_everything)
{
    uint8_t params[0x16]; dtte_t dtte; m68k_ptr_t owner = 0x11, txbuf = 0x22, ctx = 0x33;
    m68k_ptr_t handlers[4] = { 0xA1, 0xA2, 0xA3, 0xA4 };
    uint8_t vtable[0x30]; m68k_ptr_t vt[4] = { 0xB1, 0xB2, 0xB3, 0xB4 };
    int i;
    reset();
    for (i = 0; i < 0x16; i++) params[i] = (uint8_t)(0x40 + i);
    memset(vtable, 0xEE, sizeof vtable); memcpy(vtable + 0x14, vt, 16);
    memset(&dtte, 0, sizeof dtte);
    SIO_$INIT_DESC(&desc, params, &dtte, &owner, &txbuf, handlers, &ctx, (char *)vtable);
    ASSERT_EQ(0x33, desc.context);
    ASSERT_EQ(0x11, desc.owner);
    ASSERT_EQ(0, memcmp(&desc.params, params, 0x16));
    ASSERT_EQ(0xA1, desc.rcv_handler); ASSERT_EQ(0xA2, desc.drain_handler);
    ASSERT_EQ(0xA3, desc.dcd_handler); ASSERT_EQ(0xA4, desc.special_rcv);
    ASSERT_EQ(0xB1, desc.output_char); ASSERT_EQ(0xB2, desc.set_params);
    ASSERT_EQ(0xB3, desc.inq_params);  ASSERT_EQ(0xB4, desc.set_break);
    ASSERT_EQ(0x22, desc.txbuf);
    ASSERT_EQ(ARCH_PTR_TO_VA(&desc), dtte.tty_handler);
    ASSERT_EQ(1, i_init_calls);
    ASSERT_PTR_EQ(&desc, i_init_arg);
}

TEST(drain_handler_record)
{
    m68k_ptr_t rec[3] = { 1, 2, 3 }; m68k_ptr_t data = 0x77, ctx = 0x88;
    reset();
    SIO_$INIT_DRAIN_HANDLER(rec, NULL, &data, &ctx);
    ASSERT_EQ(ARCH_PTR_TO_VA(TTY_$I_OUTPUT_BUFFER_DRAINED), rec[0]);
    ASSERT_EQ(0x88, rec[1]);
    ASSERT_EQ(0x77, rec[2]);
}

TEST(init_dtte_order_and_fields)
{
    dtte_t dtte;
    reset();
    memset(&dtte, 0xFF, sizeof dtte);
    SIO_$INIT_DTTE(&dtte, 2);
    ASSERT_EQ(3, ec_init_calls);
    ASSERT_PTR_EQ((uint8_t *)&dtte + 0x0C, ec_init_args[0]);
    ASSERT_PTR_EQ((uint8_t *)&dtte + 0x18, ec_init_args[1]);
    ASSERT_PTR_EQ(&dtte, ec_init_args[2]);
    ASSERT_EQ(0, dtte.flags);
    ASSERT_EQ(2, dtte.discipline);
}

TEST(inq_param_copies_then_asks_driver)
{
    int16_t line = 4; uint32_t mask = 0x2001; status_$t st = -1; sio_params_t out;
    reset();
    desc.context = 0xC1;
    desc.params.flags1 = 0x11; desc.params.baud_rate = 9600; desc.params.parity = 3;
    desc.inq_params = fn_va((void (*)(void))mock_inq_params);
    SIO_$K_INQ_PARAM(&line, &out, &mask, &st);
    ASSERT_EQ(4, gd_line);
    ASSERT_EQ(0, memcmp(&out, &desc.params, sizeof out));
    ASSERT_EQ(1, iq_calls);
    ASSERT_EQ(0xC1, iq_ctx);
    ASSERT_PTR_EQ(&out, iq_p);
    ASSERT_EQ(0x2001, iq_mask);
    ASSERT_EQ(0x1234, st);
}

TEST(inq_param_lookup_failure_returns_early)
{
    int16_t line = 4; uint32_t mask = 1; status_$t st = 0; sio_params_t out;
    reset();
    gd_status = 0xB000D;
    desc.inq_params = fn_va((void (*)(void))mock_inq_params);
    SIO_$K_INQ_PARAM(&line, &out, &mask, &st);
    ASSERT_EQ(0xB000D, st);
    ASSERT_EQ(0, iq_calls);
}

TEST(rcv_no_errors_goes_to_rcv_handler)
{
    reset();
    desc.owner = 0x99;
    desc.rcv_handler = fn_va((void (*)(void))mock_rcv);
    desc.data_rcv = fn_va((void (*)(void))mock_data_rcv);
    SIO_$I_RCV(&desc, 'x', 0);
    ASSERT_EQ(1, rcv_calls);
    ASSERT_EQ(0x99, rcv_owner);
    ASSERT_EQ('x', rcv_ch);
    ASSERT_EQ(0, drcv_calls);
    ASSERT_EQ(0, desc.pending_int);
}

TEST(rcv_errors_masked_and_dispatched)
{
    reset();
    desc.rcv_handler = fn_va((void (*)(void))mock_rcv);
    desc.data_rcv = fn_va((void (*)(void))mock_data_rcv);
    desc.params.break_mask = 0x03;           /* only bits 0,1 enabled */
    SIO_$I_RCV(&desc, 'y', 0x07);
    ASSERT_EQ(0x03, desc.pending_int);
    ASSERT_EQ(1, drcv_calls);
    ASSERT_EQ('y', drcv_ch);
    ASSERT_EQ(0, rcv_calls);
    /* error bits all masked out: pending stays 0, rcv_handler gets it */
    desc.pending_int = 0;
    SIO_$I_RCV(&desc, 'z', 0x04);
    ASSERT_EQ(0, desc.pending_int);
    ASSERT_EQ(1, rcv_calls);
    ASSERT_EQ('z', rcv_ch);
}

TEST(rcv_special_handler_needs_bit5_and_flag)
{
    reset();
    desc.owner = 0x55;
    desc.rcv_handler = fn_va((void (*)(void))mock_rcv);
    desc.special_rcv = fn_va((void (*)(void))mock_special_rcv);
    desc.params.break_mask = 0xFF;
    SIO_$I_RCV(&desc, 'a', 0x20);            /* bit 5, RECV_ERROR not set */
    ASSERT_EQ(0, srcv_calls);
    desc.params.flags2 = SIO_CTRL_RECV_ERROR;
    SIO_$I_RCV(&desc, 'a', 0x20);
    ASSERT_EQ(1, srcv_calls);
    ASSERT_EQ(0x55, srcv_owner);
    ASSERT_EQ(2, rcv_calls);                 /* no data_rcv -> rcv_handler both times */
}

TEST(xmit_done_reports_restart)
{
    reset();
    desc.state = SIO_XMIT_ACTIVE | 0x0100;
    tstart_sets = 0;
    ASSERT_EQ(0, (uint8_t)SIO_$I_XMIT_DONE(&desc));
    ASSERT_EQ(0x0100, desc.state);
    ASSERT_EQ(1, tstart_calls);
    tstart_sets = SIO_XMIT_ACTIVE;
    ASSERT_EQ(0xFF, (uint8_t)SIO_$I_XMIT_DONE(&desc));
    ASSERT_EQ(0x0101, desc.state);
}

int main(void)
{
    printf("SIO init helper tests\n");
    RUN_TEST(init_desc_copies_everything);
    RUN_TEST(drain_handler_record);
    RUN_TEST(init_dtte_order_and_fields);
    RUN_TEST(inq_param_copies_then_asks_driver);
    RUN_TEST(inq_param_lookup_failure_returns_early);
    RUN_TEST(rcv_no_errors_goes_to_rcv_handler);
    RUN_TEST(rcv_errors_masked_and_dispatched);
    RUN_TEST(rcv_special_handler_needs_bit5_and_flag);
    RUN_TEST(xmit_done_reports_restart);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
