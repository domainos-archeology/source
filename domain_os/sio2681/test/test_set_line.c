/*
 * sio2681/test/test_set_line.c - SIO2681_$SET_LINE (0x00E1D250),
 * SIO2681_$TONE (0x00E1D172), SIO2681_$XMIT (0x00E1D4FC)
 *
 * Register writes are captured in order so the command sequences the
 * image issues can be checked, not just the final register contents.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int __host_intr_disable_count = 0;

#include "sio2681/sio2681_internal.h"

void SIO2681_$INT1_RTE(void) { }
void SIO2681_$INT2_RTE(void) { }
static int lock_calls, unlock_calls;
ml_$spin_token_t ML_$SPIN_LOCK(void *lock) { (void)lock; lock_calls++; return 0x77; }
void (ML_$SPIN_UNLOCK)(void *lock, uint32_t token_slot) { ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token; (void)lock; (void)token; unlock_calls++; }

#include "../sio2681_data.c"
#include "../set_baud_rate.c"
#include "../set_line.c"
#include "../tone.c"
#include "../xmit.c"

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

static uint8_t regs[0x20];
static sio2681_chip_t chip;
static sio2681_channel_t chan_a, chan_b;
static sio_desc_t desc_a, desc_b;

static void reset(void)
{
    memset(regs, 0, sizeof regs);
    memset(&chip, 0, sizeof chip); memset(&chan_a, 0, sizeof chan_a); memset(&chan_b, 0, sizeof chan_b);
    chip.regs = regs;
    chan_a.regs = regs; chan_a.chip = &chip; chan_a.peer = &chan_b; chan_a.sio_desc = &desc_a;
    chan_a.chan_flags = SIO2681_CHAN_FLAG_A | SIO2681_CHAN_FLAG_BIT0;
    chan_b.regs = regs + 0x10; chan_b.chip = &chip; chan_b.peer = &chan_a; chan_b.sio_desc = &desc_b;
    chan_b.chan_flags = 0; chan_b.int_bit = 4;
    lock_calls = unlock_calls = 0;
}

TEST(set_line_no_selectors_still_resets)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p);
    SIO2681_$SET_LINE(&chan_a, &p, 0, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x45, regs[SIO2681_REG_CRA]);          /* last CR write */
    ASSERT_EQ(SIO2681_CHAN_FLAG_A, chan_a.chan_flags); /* bit 0 cleared */
    ASSERT_EQ(1, lock_calls); ASSERT_EQ(1, unlock_calls);
}

TEST(set_line_speed_rejections)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p);
    p.baud_rate = 0x00090009;                         /* index 9: baud_bits 0 */
    SIO2681_$SET_LINE(&chan_a, &p, 0x01, &st);
    ASSERT_EQ(0x360008, st);
    /* peer supports only the extended-set bit 2; rate 13 has bits 1 -> rejected unless bit 1 of the mask */
    chan_b.baud_support = 0x0002;
    p.baud_rate = 0x000D000D; st = -1;
    SIO2681_$SET_LINE(&chan_a, &p, 0x01, &st);
    ASSERT_EQ(0x360008, st);
    st = -1;
    SIO2681_$SET_LINE(&chan_a, &p, 0x02, &st);
    ASSERT_EQ(0, st);
    /* an index above 0x10 silently skips the speed arm */
    p.baud_rate = 0x00110000; st = -1; regs[SIO2681_REG_CSRA] = 0xEE;
    SIO2681_$SET_LINE(&chan_a, &p, 0x01, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0xEE, regs[SIO2681_REG_CSRA]);
}

TEST(set_line_speed_programs_channel_and_peer)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p);
    chip.config1 = 0x0000;                            /* standard set */
    chan_b.baud_support = 0x0002;                     /* peer only in the extended set */
    p.baud_rate = 0x000E000F;                         /* tx 9600, rx 19200: bits 3, in both sets */
    SIO2681_$SET_LINE(&chan_a, &p, 0x03, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x0000, chip.config1);                  /* rate in current set -> stays standard */
    ASSERT_EQ(0xCB, regs[SIO2681_REG_CSRA]);          /* rx code 0xC high, tx code 0xB low */
    ASSERT_EQ(0x0003, chan_a.baud_support);
    /* the peer (support 2) is not in the standard set (mask 1): reprogrammed at 0xE/0xE */
    ASSERT_EQ(0xBB, regs[0x10 + SIO2681_REG_CSRA]);
    ASSERT_EQ(0x0003, chan_b.baud_support);
}

TEST(set_line_speed_switches_set)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p);
    chip.config1 = 0x0000;                            /* standard set */
    chan_b.baud_support = 0x0003;
    p.baud_rate = 0x000F000F;                         /* index 15: bits 2 = extended only */
    SIO2681_$SET_LINE(&chan_a, &p, 0x03, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x8000, chip.config1);                  /* switched to the extended set */
    ASSERT_EQ(0x80, regs[SIO2681_REG_ACR]);
    ASSERT_EQ(0x00, regs[0x10 + SIO2681_REG_CSRA]);   /* peer (support 3) is in the new set: untouched */
}

TEST(set_line_mode_registers)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p);
    p.parity = 0; p.char_size = 3; p.stop_bits = 1; p.flags2 = 0x02;
    SIO2681_$SET_LINE(&chan_a, &p, 0x04, &st);
    /* MR1: 0x0B & 0xE7 | 0x10 = 0x13, | 3 -> 0x13; MR2: 0x07 | 0x10 -> 0x17, &0xF0|7 -> 0x17 */
    ASSERT_EQ(0x17, regs[SIO2681_REG_MRA]);           /* last MR write = MR2 */
    p.parity = 1; p.char_size = 0; p.stop_bits = 2; p.flags2 = 0;
    SIO2681_$SET_LINE(&chan_a, &p, 0x400, &st);
    ASSERT_EQ(0x08, regs[SIO2681_REG_MRA]);           /* MR2 = 0x07 & 0xF0 | 8 */
    /* char_size >= 4 and stop_bits 0 leave the template bits alone */
    p.parity = 2; p.char_size = 7; p.stop_bits = 0;
    SIO2681_$SET_LINE(&chan_a, &p, 0x08, &st);
    ASSERT_EQ(0x07, regs[SIO2681_REG_MRA]);
}

TEST(set_line_output_port_by_channel)
{
    sio_params_t p; status_$t st = -1;
    reset(); memset(&p, 0, sizeof p);
    chip.config2 = 0xF0AA;
    p.flags1 = 0x09;                                  /* RTS (bit 3) and DTR (bit 0) up */
    SIO2681_$SET_LINE(&chan_a, &p, 0x20, &st);
    ASSERT_EQ(0xF5AA, chip.config2);                  /* A: bits 2 and 0 */
    ASSERT_EQ(0xF5, regs[SIO2681_REG_SOPBC]);
    ASSERT_EQ(0x0A, regs[SIO2681_REG_ROPBC]);
    p.flags1 = 0x08;
    SIO2681_$SET_LINE(&chan_b, &p, 0x40, &st);
    ASSERT_EQ(0xFDAA, chip.config2);                  /* B: bit 3 set, bit 1 clear */
}

TEST(tone_drives_op7)
{
    sio2681_channel_t *cell = &chan_a; uint8_t enable; status_$t st = 0;
    reset();
    chip.config2 = 0x7F00;
    enable = 0x80;
    SIO2681_$TONE(&cell, &enable, &st);
    ASSERT_EQ(0x7F00, chip.config2);                  /* enabled -> bit 7 low */
    ASSERT_EQ(0x7F, regs[SIO2681_REG_SOPBC]);
    ASSERT_EQ(0x80, regs[SIO2681_REG_ROPBC]);
    enable = 0x00;
    SIO2681_$TONE(&cell, &enable, &st);
    ASSERT_EQ(0xFF00, chip.config2);
    ASSERT_EQ(2, lock_calls); ASSERT_EQ(2, unlock_calls);
}

TEST(xmit_enables_txrdy_once)
{
    reset();
    chip.imr_shadow = 0x22;
    SIO2681_$XMIT(&chan_b, 'q');
    ASSERT_EQ('q', regs[0x10 + SIO2681_REG_THRA]);
    ASSERT_EQ(0x32, chip.imr_shadow);
    ASSERT_EQ(0x32, regs[SIO2681_REG_IMR]);
    regs[SIO2681_REG_IMR] = 0;
    SIO2681_$XMIT(&chan_b, 'r');
    ASSERT_EQ(0x32, chip.imr_shadow);
    ASSERT_EQ(0, regs[SIO2681_REG_IMR]);              /* not rewritten */
}

int main(void)
{
    printf("SIO2681 set_line/tone/xmit tests\n");
    RUN_TEST(set_line_no_selectors_still_resets);
    RUN_TEST(set_line_speed_rejections);
    RUN_TEST(set_line_speed_programs_channel_and_peer);
    RUN_TEST(set_line_speed_switches_set);
    RUN_TEST(set_line_mode_registers);
    RUN_TEST(set_line_output_port_by_channel);
    RUN_TEST(tone_drives_op7);
    RUN_TEST(xmit_enables_txrdy_once);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
