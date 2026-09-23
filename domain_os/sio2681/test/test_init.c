/*
 * sio2681/test/test_init.c - unit tests for SIO2681_$INIT
 *
 * Compiles the real sio2681/init.c and checks the objects it fills in against
 * the listing at 0x00E333DC, in particular the single 1-based SIO2681_$PTRS
 * table recovered for bead source-jvc9 and the 1-based interrupt-stub table
 * at 0x00E351EC.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

#include "sio2681/sio2681_internal.h"

/* ==========================================================================
 * Globals and callees
 * ========================================================================== */

sio2681_global_data_t SIO2681_$DATA;

/*
 * The two interrupt stubs are hand-written assembly (sio2681/sau2/int_rte.s);
 * on the host they only need addresses that the vector store can copy.
 */
void SIO2681_$INT1_RTE(void) { }
void SIO2681_$INT2_RTE(void) { }

sio2681_ptrs_entry_t SIO2681_$PTRS[SIO2681_MAX_CHIPS];

void (*const SIO2681_$INT_VECTORS[SIO2681_MAX_CHIPS])(void) = {
    SIO2681_$INT1_RTE,
    SIO2681_$INT2_RTE,
};

static int set_line_calls;
static sio2681_channel_t *set_line_chan[2];
static sio_params_t *set_line_params[2];
static uint32_t set_line_mask[2];

void SIO2681_$SET_LINE(sio2681_channel_t *channel, sio_params_t *params,
                       uint32_t change_mask, status_$t *status_ret)
{
    if (set_line_calls < 2) {
        set_line_chan[set_line_calls] = channel;
        set_line_params[set_line_calls] = params;
        set_line_mask[set_line_calls] = change_mask;
    }
    set_line_calls++;
    *status_ret = status_$ok;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../init.c"

/* ==========================================================================
 * Fixture
 * ========================================================================== */

/*
 * The DUART registers live at 0xFFB000 and the m68k vector table at 0, so the
 * test points the host VA base at an arena big enough to cover both.  All the
 * pointer fields the code writes are plain host pointers, only the vector
 * store and the register base go through the arena.
 */
#define ARENA_SIZE 0x00FFB100

static uint8_t arena[ARENA_SIZE];

static sio2681_chip_t chip;
static sio2681_channel_t chan_a, chan_b;
static sio_desc_t desc_a, desc_b;
static sio_desc_t *desc_a_ptr, *desc_b_ptr;
static sio_params_t params_a, params_b;
static uint16_t config[3];

static void setup(void)
{
    memset(arena, 0, ARENA_SIZE);
    memset(&chip, 0, sizeof(chip));
    memset(&chan_a, 0, sizeof(chan_a));
    memset(&chan_b, 0, sizeof(chan_b));
    memset(SIO2681_$PTRS, 0, sizeof(SIO2681_$PTRS));
    set_line_calls = 0;
    desc_a_ptr = &desc_a;
    desc_b_ptr = &desc_b;
    config[0] = 0x1111;
    config[1] = 0x2222;
    config[2] = 0x3333;
}

static m68k_ptr_t *vector_table(void)
{
    return (m68k_ptr_t *)ARCH_VA_TO_PTR(0x64);
}

static volatile uint8_t *chip_regs(int16_t chip_num)
{
    return (volatile uint8_t *)ARCH_VA_TO_PTR(SIO2681_BASE_ADDR - 0x20 +
                                              (chip_num << 5));
}

static void run_init(int16_t vec_num, int16_t chip_num)
{
    SIO2681_$INIT(&vec_num, &chip_num,
                  &chan_a, &desc_a_ptr, &params_a,
                  &chan_b, &desc_b_ptr, &params_b,
                  &chip, config);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * 0xE33428/0xE3342C/0xE3346E all index 0x00E2DF80 with chip_num*0x10 at a
 * NEGATIVE displacement, so chip 1 is entry [0].
 */
TEST(chip_one_fills_the_first_ptrs_entry)
{
    setup();
    run_init(1, 1);

    ASSERT_EQ((uintptr_t)&chan_a, (uintptr_t)SIO2681_$PTRS[0].chan_a);
    ASSERT_EQ((uintptr_t)&chan_b, (uintptr_t)SIO2681_$PTRS[0].chan_b);
    ASSERT_EQ((uintptr_t)&chip,   (uintptr_t)SIO2681_$PTRS[0].chip);
    ASSERT_EQ(0, SIO2681_$PTRS[0].saved_pc);   /* only the stubs write this */

    /* the second entry is untouched */
    ASSERT_EQ(0, (uintptr_t)SIO2681_$PTRS[1].chan_a);
}

TEST(chip_two_fills_the_second_ptrs_entry)
{
    setup();
    run_init(2, 2);

    ASSERT_EQ(0, (uintptr_t)SIO2681_$PTRS[0].chan_a);
    ASSERT_EQ((uintptr_t)&chan_a, (uintptr_t)SIO2681_$PTRS[1].chan_a);
    ASSERT_EQ((uintptr_t)&chan_b, (uintptr_t)SIO2681_$PTRS[1].chan_b);
    ASSERT_EQ((uintptr_t)&chip,   (uintptr_t)SIO2681_$PTRS[1].chip);
}

/* 0xE333F4..0xE333FC: chip 1 sits at 0xFFB000, chip 2 at 0xFFB020. */
TEST(register_base_is_one_based)
{
    setup();
    run_init(1, 1);
    ASSERT_EQ((uintptr_t)ARCH_VA_TO_PTR(0xFFB000), (uintptr_t)chip.regs);
    ASSERT_EQ((uintptr_t)ARCH_VA_TO_PTR(0xFFB000), (uintptr_t)chan_a.regs);
    ASSERT_EQ((uintptr_t)ARCH_VA_TO_PTR(0xFFB010), (uintptr_t)chan_b.regs);

    setup();
    run_init(1, 2);
    ASSERT_EQ((uintptr_t)ARCH_VA_TO_PTR(0xFFB020), (uintptr_t)chip.regs);
}

/* 0xE33406/0xE3340A: the config words at +0 and +4, and the 0xA2 IMR shadow. */
TEST(chip_record_is_filled_from_config)
{
    setup();
    run_init(1, 1);

    ASSERT_EQ(0x1111, chip.config1);
    ASSERT_EQ(0x3333, chip.config2);
    ASSERT_EQ(0xA2, chip.imr_shadow);
}

/*
 * 0xE33440/0xE33446 for A and 0xE33486/0xE3348A for B: the word at +0x18 is
 * 2 on channel A and 0 on channel B, and the word at +0x12 is 0 and 4.  An
 * earlier decompilation had these on the +0x10 flags field instead.
 */
TEST(channel_words_18_and_12)
{
    setup();
    run_init(1, 1);

    ASSERT_EQ(0x0002, chan_a.chan_flags);
    ASSERT_EQ(0x0000, chan_a.int_bit);
    ASSERT_EQ(0x0000, chan_a.flags);

    ASSERT_EQ(0x0000, chan_b.chan_flags);
    ASSERT_EQ(0x0004, chan_b.int_bit);
    ASSERT_EQ(0x0000, chan_b.flags);

    ASSERT_EQ((uintptr_t)&chan_b, (uintptr_t)chan_a.peer);
    ASSERT_EQ((uintptr_t)&chan_a, (uintptr_t)chan_b.peer);
    ASSERT_EQ((uintptr_t)&desc_a, (uintptr_t)chan_a.sio_desc);
    ASSERT_EQ((uintptr_t)&desc_b, (uintptr_t)chan_b.sio_desc);
}

/* 0xE334B4 / 0xE334CE: "pea (0x3fff).w" pushes the CONSTANT 0x3FFF. */
TEST(set_line_is_called_for_both_channels_with_the_full_mask)
{
    setup();
    run_init(1, 1);

    ASSERT_EQ(2, set_line_calls);
    ASSERT_EQ((uintptr_t)&chan_a, (uintptr_t)set_line_chan[0]);
    ASSERT_EQ((uintptr_t)&params_a, (uintptr_t)set_line_params[0]);
    ASSERT_EQ(0x3FFF, set_line_mask[0]);
    ASSERT_EQ((uintptr_t)&chan_b, (uintptr_t)set_line_chan[1]);
    ASSERT_EQ((uintptr_t)&params_b, (uintptr_t)set_line_params[1]);
    ASSERT_EQ(0x3FFF, set_line_mask[1]);
}

/*
 * 0xE334F0: source index chip_num*4 off 0x00E351EC-4, destination
 * vec_num*4 off 0x64-4.  Both are 1-based.
 */
TEST(interrupt_vector_is_installed_one_based)
{
    setup();
    run_init(3, 2);

    ASSERT_EQ(ARCH_PTR_TO_VA((const void *)SIO2681_$INT2_RTE),
              vector_table()[2]);
    ASSERT_EQ(0, vector_table()[0]);
    ASSERT_EQ(0, vector_table()[1]);

    setup();
    run_init(1, 1);
    ASSERT_EQ(ARCH_PTR_TO_VA((const void *)SIO2681_$INT1_RTE),
              vector_table()[0]);
}

/* 0xE33418 clears IMR, 0xE334FC writes the shadow back last. */
TEST(imr_is_cleared_then_restored)
{
    setup();
    run_init(1, 1);

    ASSERT_EQ(0xA2, chip_regs(1)[SIO2681_REG_IMR]);
    /* both channels were enabled with 0x05 */
    ASSERT_EQ(0x05, chip_regs(1)[SIO2681_REG_CRA]);
    ASSERT_EQ(0x05, chip_regs(1)[0x10 + SIO2681_REG_CRA]);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)arena;

    printf("=== SIO2681_$INIT tests ===\n");

    RUN_TEST(chip_one_fills_the_first_ptrs_entry);
    RUN_TEST(chip_two_fills_the_second_ptrs_entry);
    RUN_TEST(register_base_is_one_based);
    RUN_TEST(chip_record_is_filled_from_config);
    RUN_TEST(channel_words_18_and_12);
    RUN_TEST(set_line_is_called_for_both_channels_with_the_full_mask);
    RUN_TEST(interrupt_vector_is_installed_one_based);
    RUN_TEST(imr_is_cleared_then_restored);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
