/*
 * time/test/test_clock.c - Unit tests for TIME_$CLOCK (0x00E2AFD6),
 * TIME_$ABS_CLOCK (0x00E2B026) and TIME_$GET_TIME_OF_DAY (0x00E2B06A)
 *
 * The real time/clock.c, time/abs_clock.c and time/get_time_of_day.c are
 * #included and driven
 * through the ARCH_HOST timer-register hooks declared in time/time.h
 * (time_$timer_read_reg / time_$timer_write_reg), so every read of
 * 0xFFAC03 / 0xFFAC05 / 0xFFAC07 the functions make lands on a modelled
 * register file.
 *
 * The cases pin the things the disassembly settles: the SIGNED 0xFE3
 * compare, the carry propagation out of the 16-bit low word, TIME_$CLOCK's
 * clamp to TIME_$CURRENT_TICK, and TIME_$ABS_CLOCK's branch into
 * TIME_$CLOCK's tail (which adds TIME_$CURRENT_CLOCKL, not TIME_$CLOCKL).
 */

#include <stdint.h>
#include <stdio.h>

#include "time/time_internal.h"

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

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

int __host_intr_disable_count = 0;

uint32_t TIME_$CLOCKH;
uint16_t TIME_$CLOCKL;
uint32_t TIME_$CURRENT_CLOCKH;
uint16_t TIME_$CURRENT_CLOCKL;
uint16_t TIME_$CURRENT_TICK;
uint32_t TIME_$CURRENT_TIME;
uint32_t TIME_$CURRENT_USEC;

/* ==========================================================================
 * Modelled timer register file (0xFFAC00 + offset)
 * ========================================================================== */

static uint8_t timer_regs[0x10];
static int timer_reads;

uint8_t time_$timer_read_reg(uint16_t offset)
{
    timer_reads++;
    return timer_regs[offset & 0x0F];
}

void time_$timer_write_reg(uint16_t offset, uint8_t value)
{
    timer_regs[offset & 0x0F] = value;
}

/* Load the down-counter (movep.w reads the high byte at +5, low at +7). */
static void set_rte_timer(uint16_t counter)
{
    timer_regs[TIME_TIMER_RTE_HI] = (uint8_t)(counter >> 8);
    timer_regs[TIME_TIMER_RTE_LO] = (uint8_t)(counter & 0xFF);
}

static void reset(void)
{
    int i;

    for (i = 0; i < 0x10; i++) {
        timer_regs[i] = 0;
    }
    timer_reads = 0;
    __host_intr_disable_count = 0;
    TIME_$CLOCKH = 0;
    TIME_$CLOCKL = 0;
    TIME_$CURRENT_CLOCKH = 0;
    TIME_$CURRENT_CLOCKL = 0;
    TIME_$CURRENT_TICK = TIME_INITIAL_TICK;
    TIME_$CURRENT_TIME = 0;
    TIME_$CURRENT_USEC = 0;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

/*
 * The timer registers are SAU2 hardware (SAU2_TIMER_BASE, arch/m68k/sau2/hw.h); on the host
 * ARCH_IO_READ8 / ARCH_IO_WRITE8 call these two hooks, which hand the
 * register offset to the model above.
 */
#define SAU2_TIMER_BASE 0x00FFAC00u
uint8_t arch_$io_read8(uint32_t addr)
{
    return time_$timer_read_reg((uint16_t)(addr - SAU2_TIMER_BASE));
}
void arch_$io_write8(uint32_t addr, uint8_t val)
{
    time_$timer_write_reg((uint16_t)(addr - SAU2_TIMER_BASE), val);
}

#include "../clock.c"
#include "../abs_clock.c"
#include "../get_time_of_day.c"

/* ==========================================================================
 * TIME_$CLOCK
 * ========================================================================== */

/* counter 0x1000 -> ~0x1000 + 0x1047 = 0x46 elapsed ticks */
TEST(clock_adds_elapsed_ticks)
{
    clock_t c = { 0xDEADBEEF, 0xBEEF };

    reset();
    set_rte_timer(0x1000);
    TIME_$CURRENT_CLOCKH = 5;
    TIME_$CURRENT_CLOCKL = 0x10;

    TIME_$CLOCK(&c);

    ASSERT_EQ(5, c.high);
    ASSERT_EQ(0x56, c.low);
    /* SR was saved and restored, not forced */
    ASSERT_EQ(0, __host_intr_disable_count);
}

/* Interrupt pending (0xFFAC03 bit 0): a whole period is folded in, then the
 * result is clamped to TIME_$CURRENT_TICK. */
TEST(clock_pending_interrupt_then_clamp)
{
    clock_t c;

    reset();
    set_rte_timer(0x1040);          /* 6 ticks elapsed */
    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_RTE_INT;
    TIME_$CURRENT_TICK = 0x1047;
    TIME_$CURRENT_CLOCKH = 1;
    TIME_$CURRENT_CLOCKL = 0x100;

    TIME_$CLOCK(&c);

    /* 6 + 0x1047 = 0x104D, clamped to 0x1047, plus 0x100 */
    ASSERT_EQ(1, c.high);
    ASSERT_EQ(0x1147, c.low);
}

/* Interrupt bit set but ticks above 0xFE3: the bit is never consulted. */
TEST(clock_ignores_interrupt_when_above_fe3)
{
    clock_t c;

    reset();
    set_rte_timer(0x0000);          /* 0x1046 ticks elapsed */
    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_RTE_INT;
    TIME_$CURRENT_TICK = 0x2000;    /* no clamp */
    TIME_$CURRENT_CLOCKL = 1;

    TIME_$CLOCK(&c);

    ASSERT_EQ(0, c.high);
    ASSERT_EQ(0x1047, c.low);
    /* 0xFFAC05 and 0xFFAC07 only; the control byte was not read */
    ASSERT_EQ(2, timer_reads);
}

/* The 0xFE3 compare is signed: a "negative" tick count still takes the
 * interrupt-pending path and adds TIME_$CURRENT_TICK with carry. */
TEST(clock_signed_compare_with_carry)
{
    clock_t c;

    reset();
    set_rte_timer(0x2000);          /* 0x1046 - 0x2000 = 0xF046 (negative) */
    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_RTE_INT;
    TIME_$CURRENT_TICK = 0x1047;
    TIME_$CURRENT_CLOCKH = 0x10;
    TIME_$CURRENT_CLOCKL = 0x20;

    TIME_$CLOCK(&c);

    /* 0xF046 + 0x1047 = 0x1008D: carry, low 0x8D; 0x8D <= 0x1047 so no clamp */
    ASSERT_EQ(0x11, c.high);
    ASSERT_EQ(0xAD, c.low);
}

/* Carry out of the final add into the high longword. */
TEST(clock_carry_from_clockl)
{
    clock_t c;

    reset();
    set_rte_timer(0x1000);          /* 0x46 ticks */
    TIME_$CURRENT_CLOCKH = 0x12345678;
    TIME_$CURRENT_CLOCKL = 0xFFF0;

    TIME_$CLOCK(&c);

    ASSERT_EQ(0x12345679, c.high);
    ASSERT_EQ(0x36, c.low);
}

/* ==========================================================================
 * TIME_$ABS_CLOCK
 * ========================================================================== */

/* Fall-through path: interrupt pending adds the constant 0x1047 (NOT
 * TIME_$CURRENT_TICK) and the result is added to TIME_$CLOCKL. */
TEST(abs_clock_pending_adds_constant_period)
{
    clock_t c;

    reset();
    set_rte_timer(0x1040);          /* 6 ticks */
    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_RTE_INT;
    TIME_$CURRENT_TICK = 0x2000;    /* must not be used */
    TIME_$CLOCKH = 7;
    TIME_$CLOCKL = 0x100;
    TIME_$CURRENT_CLOCKH = 0x77;    /* must not be used */
    TIME_$CURRENT_CLOCKL = 0x7700;

    TIME_$ABS_CLOCK(&c);

    ASSERT_EQ(7, c.high);
    ASSERT_EQ(0x6 + 0x1047 + 0x100, c.low);
}

/* Fall-through path without a pending interrupt. */
TEST(abs_clock_plain)
{
    clock_t c;

    reset();
    set_rte_timer(0x1000);          /* 0x46 ticks */
    TIME_$CLOCKH = 9;
    TIME_$CLOCKL = 0xFFF0;
    TIME_$CURRENT_CLOCKL = 0;

    TIME_$ABS_CLOCK(&c);

    ASSERT_EQ(10, c.high);          /* carry from 0xFFF0 + 0x46 */
    ASSERT_EQ(0x36, c.low);
}

/* Ticks above 0xFE3: `bgt.b 0x00e2b008` lands in TIME_$CLOCK's tail, which
 * clamps to TIME_$CURRENT_TICK and adds TIME_$CURRENT_CLOCKL to A1 =
 * TIME_$CLOCKH. */
TEST(abs_clock_shared_tail_uses_current_clockl)
{
    clock_t c;

    reset();
    set_rte_timer(0x0000);          /* 0x1046 ticks */
    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_RTE_INT;   /* not consulted */
    TIME_$CURRENT_TICK = 0x1000;    /* clamps 0x1046 down */
    TIME_$CLOCKH = 0x40;
    TIME_$CLOCKL = 0x0F00;          /* must not be used */
    TIME_$CURRENT_CLOCKH = 0x99;    /* must not be used */
    TIME_$CURRENT_CLOCKL = 0x0100;

    TIME_$ABS_CLOCK(&c);

    ASSERT_EQ(0x40, c.high);
    ASSERT_EQ(0x1100, c.low);
    ASSERT_EQ(2, timer_reads);
}

/* Same tail, with the carry going into TIME_$CLOCKH. */
TEST(abs_clock_shared_tail_carry)
{
    clock_t c;

    reset();
    set_rte_timer(0x0000);          /* 0x1046 ticks */
    TIME_$CURRENT_TICK = 0x1047;    /* no clamp */
    TIME_$CLOCKH = 0x40;
    TIME_$CURRENT_CLOCKL = 0xFFF0;

    TIME_$ABS_CLOCK(&c);

    ASSERT_EQ(0x41, c.high);
    ASSERT_EQ((uint16_t)(0x1046 + 0xFFF0), c.low);
}

/* ==========================================================================
 * TIME_$GET_TIME_OF_DAY
 * ========================================================================== */

/* 0x46 ticks = 280 us added to the stored microseconds. */
TEST(gtod_adds_elapsed_microseconds)
{
    uint32_t tv[2] = { 0xDEAD, 0xBEEF };

    reset();
    set_rte_timer(0x1000);
    TIME_$CURRENT_TIME = 1000;
    TIME_$CURRENT_USEC = 500;

    TIME_$GET_TIME_OF_DAY(tv);

    ASSERT_EQ(1000, tv[0]);
    ASSERT_EQ(500 + 0x46 * 4, tv[1]);
    ASSERT_EQ(0, __host_intr_disable_count);
}

/* Microsecond carry: 1000000 exactly rolls over (the bmi test is on
 * D0 - 1000000 being negative). */
TEST(gtod_carries_into_seconds)
{
    uint32_t tv[2];

    reset();
    set_rte_timer(0x1000);          /* 0x46 ticks = 280 us */
    TIME_$CURRENT_TIME = 1000;
    TIME_$CURRENT_USEC = 1000000 - 280;

    TIME_$GET_TIME_OF_DAY(tv);

    ASSERT_EQ(1001, tv[0]);
    ASSERT_EQ(0, tv[1]);
}

/* Pending interrupt: TIME_$CURRENT_TICK is folded in, then clamped. */
TEST(gtod_pending_interrupt_clamps)
{
    uint32_t tv[2];

    reset();
    set_rte_timer(0x1040);          /* 6 ticks */
    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_RTE_INT;
    TIME_$CURRENT_TICK = 0x1047;
    TIME_$CURRENT_TIME = 5;
    TIME_$CURRENT_USEC = 0;

    TIME_$GET_TIME_OF_DAY(tv);

    ASSERT_EQ(5, tv[0]);
    ASSERT_EQ(0x1047 * 4, tv[1]);
}

/* A carry out of the 16-bit tick add on the pending path bumps SECONDS. */
TEST(gtod_tick_carry_bumps_seconds)
{
    uint32_t tv[2];

    reset();
    set_rte_timer(0x2000);          /* 0xF046, "negative" */
    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_RTE_INT;
    TIME_$CURRENT_TICK = 0x1047;
    TIME_$CURRENT_TIME = 5;
    TIME_$CURRENT_USEC = 0;

    TIME_$GET_TIME_OF_DAY(tv);

    /* 0xF046 + 0x1047 = 0x1008D -> carry, ticks 0x8D */
    ASSERT_EQ(6, tv[0]);
    ASSERT_EQ(0x8D * 4, tv[1]);
}

int main(void)
{
    printf("test_clock:\n");

    RUN_TEST(clock_adds_elapsed_ticks);
    RUN_TEST(clock_pending_interrupt_then_clamp);
    RUN_TEST(clock_ignores_interrupt_when_above_fe3);
    RUN_TEST(clock_signed_compare_with_carry);
    RUN_TEST(clock_carry_from_clockl);
    RUN_TEST(abs_clock_pending_adds_constant_period);
    RUN_TEST(abs_clock_plain);
    RUN_TEST(abs_clock_shared_tail_uses_current_clockl);
    RUN_TEST(abs_clock_shared_tail_carry);
    RUN_TEST(gtod_adds_elapsed_microseconds);
    RUN_TEST(gtod_carries_into_seconds);
    RUN_TEST(gtod_pending_interrupt_clamps);
    RUN_TEST(gtod_tick_carry_bumps_seconds);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
