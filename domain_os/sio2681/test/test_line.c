/*
 * sio2681/test/test_line.c - the 2681 driver's line routines
 *
 *   SIO2681_$INQ_LINE      0x00E725B0    SIO2681_$SET_BREAK  0x00E1D114
 *   sio2681_set_baud_rate  0x00E1D1DA    SIO2681_$INT        0x00E1CEEC
 *
 * The chip's 32 registers are a host array and SIO2681_$DATA is the image's
 * table (sio2681_data.c is included so the baud tables are real).  For the
 * interrupt loop the ISR is a scripted sequence: every SIO-layer mock the
 * loop calls advances it, standing in for the hardware clearing conditions.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int __host_intr_disable_count = 0;

#include "sio2681/sio2681_internal.h"

void SIO2681_$INT1_RTE(void) { }
void SIO2681_$INT2_RTE(void) { }

static uint8_t regs[0x20];
static uint8_t isr_script[8]; static int isr_idx;
static void next_isr(void) { regs[SIO2681_REG_ISR] = isr_script[isr_idx < 7 ? isr_idx++ : 7]; }

static int lock_calls, unlock_calls;
ml_$spin_token_t ML_$SPIN_LOCK(void *lock) { (void)lock; lock_calls++; return 0x77; }
void (ML_$SPIN_UNLOCK)(void *lock, uint32_t token_slot) { ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token; (void)lock; (void)token; unlock_calls++; }

static int tstart_calls; static sio_desc_t *tstart_arg;
void SIO_$I_TSTART(sio_desc_t *desc) { tstart_calls++; tstart_arg = desc; }

static int rcv_calls; static sio_desc_t *rcv_desc[4]; static uint8_t rcv_ch[4]; static uint32_t rcv_err[4];
void SIO_$I_RCV(sio_desc_t *desc, uint8_t ch, uint32_t err)
{ if (rcv_calls < 4) { rcv_desc[rcv_calls] = desc; rcv_ch[rcv_calls] = ch; rcv_err[rcv_calls] = err; } rcv_calls++; next_isr(); }
static int xd_calls; static boolean xd_ret;
boolean SIO_$I_XMIT_DONE(sio_desc_t *desc) { (void)desc; xd_calls++; next_isr(); return xd_ret; }
static int cts_calls; static int8_t cts_state[2]; static sio_desc_t *cts_desc[2];
void SIO_$I_CTS_CHANGE(sio_desc_t *desc, int8_t s) { if (cts_calls < 2) { cts_desc[cts_calls] = desc; cts_state[cts_calls] = s; } cts_calls++; next_isr(); }
static int dcd_calls; static int8_t dcd_state[2]; static sio_desc_t *dcd_desc[2];
void SIO_$I_DCD_CHANGE(sio_desc_t *desc, int8_t s) { if (dcd_calls < 2) { dcd_desc[dcd_calls] = desc; dcd_state[dcd_calls] = s; } dcd_calls++; next_isr(); }
static int pch_calls; static uint32_t *pch_arg;
void PCHIST_$INTERRUPT(uint32_t *pc_ptr) { pch_calls++; pch_arg = pc_ptr; }
static int xmit_calls; static sio2681_channel_t *xmit_chan; static uint8_t xmit_ch;
void SIO2681_$XMIT(sio2681_channel_t *channel, uint8_t ch) { xmit_calls++; xmit_chan = channel; xmit_ch = ch; next_isr(); }

#include "../sio2681_data.c"
#include "../inq_line.c"
#include "../set_baud_rate.c"
#include "../set_break.c"
#include "../int.c"

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

static sio2681_chip_t chip;
static sio2681_channel_t chan_a, chan_b;
static sio_desc_t desc_a, desc_b;
static sio2681_ptrs_entry_t entry;

static void reset(void)
{
    memset(regs, 0, sizeof regs); memset(isr_script, 0, sizeof isr_script); isr_idx = 0;
    memset(&chip, 0, sizeof chip); memset(&chan_a, 0, sizeof chan_a); memset(&chan_b, 0, sizeof chan_b);
    chip.regs = regs;
    chan_a.regs = regs; chan_a.chip = &chip; chan_a.peer = &chan_b; chan_a.sio_desc = &desc_a;
    chan_a.chan_flags = SIO2681_CHAN_FLAG_A; chan_a.int_bit = 0;
    chan_b.regs = regs + 0x10; chan_b.chip = &chip; chan_b.peer = &chan_a; chan_b.sio_desc = &desc_b;
    chan_b.chan_flags = 0; chan_b.int_bit = 4;
    entry.chan_a = &chan_a; entry.chan_b = &chan_b; entry.chip = &chip; entry.saved_pc = 0;
    lock_calls = unlock_calls = tstart_calls = rcv_calls = xd_calls = cts_calls = dcd_calls = 0;
    pch_calls = xmit_calls = 0; xd_ret = 0;
}

/* ---- INQ_LINE --------------------------------------------------------------- */

TEST(inq_line_no_selector)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p); p.flags1 = 0x0F;
    SIO2681_$INQ_LINE(&chan_a, &p, 0x07F, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x0F, p.flags1);
}

TEST(inq_line_channel_a_bits)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p);
    chan_a.reserved_14 = 0x100;
    regs[SIO2681_REG_IPR] = 0x0A;               /* IP0 low (CTS A up), IP2 low (DCD A up) */
    SIO2681_$INQ_LINE(&chan_a, &p, 0x180, &st);
    ASSERT_EQ(0x106, p.flags1);
    regs[SIO2681_REG_IPR] = 0x05;               /* both A lines high */
    p.flags1 = 0x06;
    SIO2681_$INQ_LINE(&chan_a, &p, 0x180, &st);
    ASSERT_EQ(0x100, p.flags1);
}

TEST(inq_line_channel_b_bits_and_override)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p);
    regs[SIO2681_REG_IPR] = 0x05;               /* IP1, IP3 low: B lines up */
    SIO2681_$INQ_LINE(&chan_b, &p, 0x180, &st);
    ASSERT_EQ(0x06, p.flags1);
    regs[SIO2681_REG_IPR] = 0x0A;               /* B lines high */
    p.flags2 = 0x40;                            /* DCD override */
    SIO2681_$INQ_LINE(&chan_b, &p, 0x080, &st); /* DCD only */
    ASSERT_EQ(0x06, p.flags1);
    p.flags2 = 0;
    SIO2681_$INQ_LINE(&chan_b, &p, 0x100, &st); /* CTS only */
    ASSERT_EQ(0x04, p.flags1);
}

/* ---- set_baud_rate ---------------------------------------------------------- */

TEST(set_baud_rate_programs_acr_csr)
{
    reset();
    chip.config1 = 0x80AA;
    sio2681_set_baud_rate(&chan_b, 14, 15, 0);   /* tx 9600 (0xB), rx 19200 (0xC), standard set */
    ASSERT_EQ(0x00AA, chip.config1);            /* bit 7 of the high byte cleared, low byte kept */
    ASSERT_EQ(0x00, regs[SIO2681_REG_ACR]);
    ASSERT_EQ(0xCB, regs[0x10 + SIO2681_REG_CSRA]);
    ASSERT_EQ(SIO2681_$DATA.baud_bits[14], chan_b.baud_support);
    sio2681_set_baud_rate(&chan_a, 2, 2, -1);    /* extended set */
    ASSERT_EQ(0x80AA, chip.config1);
    ASSERT_EQ(0x80, regs[SIO2681_REG_ACR]);
    ASSERT_EQ(0x11, regs[SIO2681_REG_CSRA]);
}

/* ---- SET_BREAK -------------------------------------------------------------- */

TEST(set_break_commands)
{
    reset();
    SIO2681_$SET_BREAK(&chan_b, -1);
    ASSERT_EQ(0x60, regs[0x10 + SIO2681_REG_CRA]);
    ASSERT_EQ(0, tstart_calls);
    SIO2681_$SET_BREAK(&chan_b, 0);
    ASSERT_EQ(0x70, regs[0x10 + SIO2681_REG_CRA]);
    ASSERT_EQ(1, tstart_calls);
    ASSERT_PTR_EQ(&desc_b, tstart_arg);
    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(2, unlock_calls);
}

/* ---- INT -------------------------------------------------------------------- */

TEST(int_masked_by_imr)
{
    reset();
    chip.imr_shadow = 0;
    regs[SIO2681_REG_ISR] = 0xFF;
    SIO2681_$INT(&entry);
    ASSERT_EQ(0, rcv_calls); ASSERT_EQ(0, xd_calls); ASSERT_EQ(0, cts_calls);
}

TEST(int_receive_b_then_a)
{
    reset();
    chip.imr_shadow = 0xFF;
    regs[SIO2681_REG_ISR] = 0x22;               /* script leaves 0 afterwards */
    regs[SIO2681_REG_SRA] = 0x35;  regs[SIO2681_REG_RHRA] = 'A';
    regs[0x10 + SIO2681_REG_SRA] = 0x85; regs[0x10 + SIO2681_REG_RHRA] = 'B';
    SIO2681_$INT(&entry);
    ASSERT_EQ(2, rcv_calls);
    ASSERT_PTR_EQ(&desc_b, rcv_desc[0]);
    ASSERT_EQ('B', rcv_ch[0]);
    ASSERT_EQ(SIO2681_$DATA.error_table[8], rcv_err[0]);
    ASSERT_PTR_EQ(&desc_a, rcv_desc[1]);
    ASSERT_EQ('A', rcv_ch[1]);
    ASSERT_EQ(SIO2681_$DATA.error_table[3], rcv_err[1]);
}

TEST(int_transmit_paths)
{
    reset();
    chip.imr_shadow = 0xFF;
    regs[SIO2681_REG_ISR] = 0x11;
    chan_a.flags = 0; xd_ret = 0;               /* A: XMIT_DONE says idle -> TxRDY A masked */
    chan_b.flags = 1;                           /* B: fill path */
    SIO2681_$INT(&entry);
    ASSERT_EQ(1, xd_calls);
    ASSERT_EQ(0xFE, chip.imr_shadow);
    /* regs[0x0B] is both IMR (write) and ISR (read); the script has overwritten it */
    ASSERT_EQ(1, pch_calls);
    ASSERT_PTR_EQ(&entry.saved_pc, pch_arg);
    ASSERT_EQ(1, xmit_calls);
    ASSERT_PTR_EQ(&chan_b, xmit_chan);
    ASSERT_EQ(0x20, xmit_ch);
}

TEST(int_transmit_still_busy_keeps_mask)
{
    reset();
    chip.imr_shadow = 0x11;
    regs[SIO2681_REG_ISR] = 0x10;
    xd_ret = (boolean)-1;
    SIO2681_$INT(&entry);
    ASSERT_EQ(1, xd_calls);
    ASSERT_EQ(0x11, chip.imr_shadow);
}

TEST(int_input_change)
{
    reset();
    chip.imr_shadow = 0x80;
    regs[SIO2681_REG_ISR] = 0x80;
    regs[SIO2681_REG_IPCR] = 0xF5;              /* all four deltas; IP0,IP2 high, IP1,IP3 low */
    SIO2681_$INT(&entry);
    ASSERT_EQ(2, cts_calls);
    ASSERT_PTR_EQ(&desc_a, cts_desc[0]); ASSERT_EQ(0, (uint8_t)cts_state[0]);
    ASSERT_PTR_EQ(&desc_b, cts_desc[1]); ASSERT_EQ(0xFF, (uint8_t)cts_state[1]);
    ASSERT_EQ(2, dcd_calls);
    ASSERT_PTR_EQ(&desc_a, dcd_desc[0]); ASSERT_EQ(0, (uint8_t)dcd_state[0]);
    ASSERT_PTR_EQ(&desc_b, dcd_desc[1]); ASSERT_EQ(0xFF, (uint8_t)dcd_state[1]);
}

int main(void)
{
    printf("SIO2681 line tests\n");
    RUN_TEST(inq_line_no_selector);
    RUN_TEST(inq_line_channel_a_bits);
    RUN_TEST(inq_line_channel_b_bits_and_override);
    RUN_TEST(set_baud_rate_programs_acr_csr);
    RUN_TEST(set_break_commands);
    RUN_TEST(int_masked_by_imr);
    RUN_TEST(int_receive_b_then_a);
    RUN_TEST(int_transmit_paths);
    RUN_TEST(int_transmit_still_busy_keeps_mask);
    RUN_TEST(int_input_change);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
