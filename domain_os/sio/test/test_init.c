/*
 * sio/test/test_init.c - SIO_$INIT (0x00E32BE0)
 *
 * The routine only computes addresses inside TERM_$DATA and hands them to
 * five helpers; the mocks record what they were given and the tests
 * compare against the strides and offsets read off the disassembly
 * (console: port*0xE4+0x1084 state, port*0x0C+0x114C drain record,
 * base+0x88 handlers, base+0x58 params, base+0x70 hw; serial: base+0x18,
 * base+0, base+0x40; both: port*0x78+0xF78 desc, port*0x4DC-0x384 line,
 * port*0x4DC+0x4E txbuf, DTTE[max_dtte]).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int __host_intr_disable_count = 0;

#include "sio/sio_internal.h"

term_data_t TERM_$DATA;

/* ---- mocks --------------------------------------------------------------- */

static int   ot_calls; static void *ot_a1, *ot_a2, *ot_a4, *ot_a6; static uint32_t ot_c3, ot_c5;
void OS_TERM_INIT(uint32_t *a1, uint32_t *a2, uint32_t *a3, uint32_t *a4, uint32_t *a5, uint32_t *a6)
{ ot_calls++; ot_a1 = a1; ot_a2 = a2; ot_c3 = *a3; ot_a4 = a4; ot_c5 = *a5; ot_a6 = a6; }

static int   il_calls; static void *il_desc, *il_dtte, *il_hw; static m68k_ptr_t il_cfg;
void SIO_$INIT_LINE(void *desc, void *port_data, m68k_ptr_t *config, void *hw_info)
{ il_calls++; il_desc = desc; il_dtte = port_data; il_cfg = *config; il_hw = hw_info; }

static int   idh_calls; static void *idh_rec, *idh_dtte; static m68k_ptr_t idh_data, idh_ctx;
void SIO_$INIT_DRAIN_HANDLER(m68k_ptr_t *handler, void *dtte, m68k_ptr_t *data_ptr, m68k_ptr_t *context_ptr)
{ idh_calls++; idh_rec = handler; idh_dtte = dtte; idh_data = *data_ptr; idh_ctx = *context_ptr; }

static int   id_calls; static void *id_desc, *id_params, *id_dtte, *id_handlers, *id_ctxp, *id_vt;
static m68k_ptr_t id_owner, id_txbuf;
void SIO_$INIT_DESC(sio_desc_t *desc, void *param_block, void *dtte, m68k_ptr_t *owner_ptr,
                    m68k_ptr_t *txbuf_ptr, m68k_ptr_t *handlers, m68k_ptr_t *context_ptr, char *vtable)
{ id_calls++; id_desc = desc; id_params = param_block; id_dtte = dtte; id_owner = *owner_ptr;
  id_txbuf = *txbuf_ptr; id_handlers = handlers; id_ctxp = context_ptr; id_vt = vtable; }

static int   idt_calls; static void *idt_dtte; static int16_t idt_disc;
void SIO_$INIT_DTTE(dtte_t *dtte, int16_t discipline) { idt_calls++; idt_dtte = dtte; idt_disc = discipline; }

static int   cf_calls; static void *cf_tty; static uint8_t cf_ch; static char cf_en;
void TTY_$I_ENABLE_CRASH_FUNC(tty_desc_t *tty, uint8_t ch, char enable)
{ cf_calls++; cf_tty = tty; cf_ch = ch; cf_en = enable; }

static int   sp_calls; static m68k_ptr_t sp_ctx; static sio_params_t *sp_params; static uint32_t sp_mask;
static status_$t sp_status_out;
static void mock_set_params(m68k_ptr_t ctx, sio_params_t *p, uint32_t mask, status_$t *st)
{ sp_calls++; sp_ctx = ctx; sp_params = p; sp_mask = mask; *st = sp_status_out; }

#include "../init.c"

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

static uint8_t *B(int32_t off) { return (uint8_t *)&TERM_$DATA + off; }
static m68k_ptr_t VA(int32_t off) { return ARCH_PTR_TO_VA(B(off)); }

static void reset(void)
{
    memset(&TERM_$DATA, 0, sizeof TERM_$DATA);
    ARCH_HOST_VA_BASE = (uintptr_t)&TERM_$DATA & ~(uintptr_t)0xFFFFFFFFu;
    ot_calls = il_calls = idh_calls = id_calls = idt_calls = cf_calls = sp_calls = 0;
    sp_status_out = 0;
}

TEST(console_port_sequence)
{
    sio_desc_t *ret = NULL; status_$t st = -1; uint32_t ctx = 1; char vt[0x30];
    reset();
    TERM_$MAX_DTTE = 2;
    SIO_$INIT(1, &ctx, vt, &ret, 0, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, ot_calls);
    ASSERT_PTR_EQ(B(1 * 0xE4 + 0x1084), ot_a1);
    ASSERT_PTR_EQ(&TERM_$DATA.dtte[2], ot_a2);
    ASSERT_EQ(VA(1 * 0x4DC - 0x384), ot_c3);
    ASSERT_PTR_EQ(B(0xC0), ot_a4);
    ASSERT_EQ(VA(1 * 0x78 + 0xF78), ot_c5);
    ASSERT_PTR_EQ(B(0xB0), ot_a6);

    ASSERT_EQ(1, il_calls);
    ASSERT_PTR_EQ(B(1 * 0x4DC - 0x384), il_desc);
    ASSERT_PTR_EQ(&TERM_$DATA.dtte[2], il_dtte);
    ASSERT_EQ(VA(1 * 0x0C + 0x114C), il_cfg);
    ASSERT_PTR_EQ(B(0x70), il_hw);

    ASSERT_EQ(1, idh_calls);
    ASSERT_PTR_EQ(B(1 * 0x0C + 0x114C), idh_rec);
    ASSERT_PTR_EQ(&TERM_$DATA.dtte[2], idh_dtte);
    ASSERT_EQ(VA(1 * 0x4DC + 0x4E), idh_data);
    ASSERT_EQ(VA(1 * 0x4DC - 0x384), idh_ctx);

    ASSERT_EQ(1, id_calls);
    ASSERT_PTR_EQ(B(1 * 0x78 + 0xF78), id_desc);
    ASSERT_PTR_EQ(B(0x58), id_params);
    ASSERT_PTR_EQ(&TERM_$DATA.dtte[2], id_dtte);
    ASSERT_EQ(VA(1 * 0xE4 + 0x1084), id_owner);
    ASSERT_EQ(VA(1 * 0xE4 + 0x1122), id_txbuf);
    ASSERT_PTR_EQ(B(0x88), id_handlers);
    ASSERT_PTR_EQ(&ctx, id_ctxp);
    ASSERT_PTR_EQ(vt, id_vt);

    ASSERT_EQ(1, idt_calls);
    ASSERT_PTR_EQ(&TERM_$DATA.dtte[2], idt_dtte);
    ASSERT_EQ(2, idt_disc);
    ASSERT_EQ(0, cf_calls);
    ASSERT_EQ(0, sp_calls);
    ASSERT_PTR_EQ(B(1 * 0x78 + 0xF78), ret);
    ASSERT_EQ(3, TERM_$MAX_DTTE);
}

TEST(serial_port_with_crash_char)
{
    sio_desc_t *ret = NULL; status_$t st = -1; uint32_t ctx = 1; char vt[0x30];
    reset();
    TERM_$MAX_DTTE = 1;
    SIO_$INIT(2, &ctx, vt, &ret, (int8_t)-1, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, ot_calls);
    ASSERT_EQ(0, idh_calls);
    ASSERT_EQ(1, il_calls);
    ASSERT_PTR_EQ(B(2 * 0x4DC - 0x384), il_desc);
    ASSERT_PTR_EQ(&TERM_$DATA.dtte[1], il_dtte);
    ASSERT_EQ(VA(2 * 0x78 + 0xF78), il_cfg);
    ASSERT_PTR_EQ(B(0x40), il_hw);
    ASSERT_EQ(1, id_calls);
    ASSERT_PTR_EQ(B(2 * 0x78 + 0xF78), id_desc);
    ASSERT_PTR_EQ(B(0), id_params);
    ASSERT_EQ(VA(2 * 0x4DC - 0x384), id_owner);
    ASSERT_EQ(VA(2 * 0x4DC + 0x4E), id_txbuf);
    ASSERT_PTR_EQ(B(0x18), id_handlers);
    ASSERT_EQ(0, idt_disc);
    ASSERT_EQ(1, cf_calls);
    ASSERT_PTR_EQ(B(2 * 0x4DC - 0x384), cf_tty);
    ASSERT_EQ(0x1B, cf_ch);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)cf_en);
    ASSERT_EQ(0, sp_calls);
    ASSERT_PTR_EQ(B(2 * 0x78 + 0xF78), ret);
    ASSERT_EQ(2, TERM_$MAX_DTTE);
}

TEST(serial_port_applies_set_params)
{
    sio_desc_t *ret = NULL; status_$t st = -1; uint32_t ctx = 1; char vt[0x30];
    sio_desc_t *desc;
    reset();
    desc = (sio_desc_t *)(void *)B(3 * 0x78 + 0xF78);
    desc->context = 0xCAFE;
    desc->set_params = (m68k_ptr_t)((uintptr_t)mock_set_params - ARCH_HOST_VA_BASE);
    sp_status_out = 0x360002;
    SIO_$INIT(3, &ctx, vt, &ret, 0, &st);
    ASSERT_EQ(1, sp_calls);
    ASSERT_EQ(0xCAFE, sp_ctx);
    ASSERT_PTR_EQ(&desc->params, sp_params);
    ASSERT_EQ(0x3FFF, sp_mask);
    ASSERT_EQ(0x360002, st);              /* the driver's status is returned as is */
    ASSERT_EQ(0, cf_calls);
    ASSERT_PTR_EQ(desc, ret);
    ASSERT_EQ(1, TERM_$MAX_DTTE);
}

int main(void)
{
    printf("SIO_$INIT tests\n");
    RUN_TEST(console_port_sequence);
    RUN_TEST(serial_port_with_crash_char);
    RUN_TEST(serial_port_applies_set_params);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
