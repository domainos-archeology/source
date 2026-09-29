/*
 * timer/test/test_init.c - Unit tests for TIMER_$INIT (0x00E16340)
 *
 * The real timer/init.c is #included together with the real
 * time/wrt_timer.c it calls, so the three counter loads and the control
 * byte sequence are observed at the modelled register file.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "timer/timer_internal.h"
#include "time/time_internal.h"

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

uint8_t IN_VT_INT;
uint8_t IN_RT_INT;

static int set_vector_calls;
void TIME_$SET_VECTOR(void) { set_vector_calls++; }

#define MAX_WRITES 16
static int write_count;
static uint16_t write_off[MAX_WRITES];
static uint8_t write_val[MAX_WRITES];

uint8_t time_$timer_read_reg(uint16_t offset) { (void)offset; return 0; }

void time_$timer_write_reg(uint16_t offset, uint8_t value)
{
    if (write_count < MAX_WRITES) {
        write_off[write_count] = offset;
        write_val[write_count] = value;
    }
    write_count++;
}

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

#include "time/wrt_timer.c"
#include "../init.c"

TEST(init_sequence)
{
    static const uint16_t exp_off[] = { 5, 7, 9, 11, 13, 15, 3, 1, 3, 1 };
    static const uint8_t exp_val[] = { 0x10, 0x46, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xE0, 0xE1, 0xE1, 0xE0 };
    int i;

    write_count = 0;
    set_vector_calls = 0;
    IN_VT_INT = 0xFF;
    IN_RT_INT = 0xFF;

    ASSERT_EQ(0, TIMER_$INIT());

    ASSERT_EQ(1, set_vector_calls);
    ASSERT_EQ(10, write_count);
    for (i = 0; i < 10; i++) {
        ASSERT_EQ(exp_off[i], write_off[i]);
        ASSERT_EQ(exp_val[i], write_val[i]);
    }
    /* the VT and aux loads cleared both flags */
    ASSERT_EQ(0, IN_VT_INT);
    ASSERT_EQ(0, IN_RT_INT);
}

TEST(constant_cells)
{
    ASSERT_EQ(0x1046, timer_$c_rte_period);
    ASSERT_EQ(3, timer_$c_index_aux);
    ASSERT_EQ(2, timer_$c_index_vt);
    ASSERT_EQ(0xFFFF, timer_$c_disabled);
    ASSERT_EQ(1, timer_$c_index_rte);
}

int main(void)
{
    printf("test_init (timer):\n");
    RUN_TEST(init_sequence);
    RUN_TEST(constant_cells);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
