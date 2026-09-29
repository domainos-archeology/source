/*
 * time/test/test_timer_regs.c - Unit tests for TIME_$VT_TIMER (0x00E2AF6C),
 * TIME_$WRT_TIMER (0x00E2AFA0), TIME_$SET_VECTOR (0x00E2B102) and
 * TIME_$VT_INT (0x00E163E4)
 *
 * The real .c files are #included and driven through the ARCH_HOST timer
 * register hooks (time/time.h) and the host vector table (arch/host/arch.h).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
uint16_t PROC1_$CURRENT;
time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];
void *arch_$vector_table[ARCH_VECTOR_COUNT];

void TIME_$TIMER_HANDLER(void) { }

/* Modelled timer register file, with a write log. */
static uint8_t timer_regs[0x10];
#define MAX_WRITES 16
static int write_count;
static uint16_t write_off[MAX_WRITES];
static uint8_t write_val[MAX_WRITES];

uint8_t time_$timer_read_reg(uint16_t offset)
{
    return timer_regs[offset & 0x0F];
}

void time_$timer_write_reg(uint16_t offset, uint8_t value)
{
    if (write_count < MAX_WRITES) {
        write_off[write_count] = offset;
        write_val[write_count] = value;
    }
    write_count++;
    timer_regs[offset & 0x0F] = value;
}

static clock_t vt_int_time = { 0x1234, 0x5678 };
void PROC1_$VT_INT(clock_t *cpu_time_out) { *cpu_time_out = vt_int_time; }

static int scan_calls;
static time_queue_t *scan_queue;
static clock_t scan_now;
static int in_vt_int_at_scan;
void TIME_$Q_SCAN_QUEUE(time_queue_t *queue, clock_t *now, status_$t *status)
{
    scan_calls++;
    scan_queue = queue;
    scan_now = *now;
    in_vt_int_at_scan = IN_VT_INT;
    *status = status_$ok;
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

#include "../vt_timer.c"
#include "../wrt_timer.c"
#include "../set_vector.c"
#include "../vt_int.c"

static void reset(void)
{
    memset(timer_regs, 0, sizeof(timer_regs));
    write_count = 0;
    IN_VT_INT = 0;
    IN_RT_INT = 0;
    scan_calls = 0;
    memset(arch_$vector_table, 0, sizeof(arch_$vector_table));
}

TEST(vt_timer_reads_counter)
{
    reset();
    timer_regs[TIME_TIMER_VT_HI] = 0x12;
    timer_regs[TIME_TIMER_VT_LO] = 0x34;
    ASSERT_EQ(0x1234, TIME_$VT_TIMER());
}

TEST(vt_timer_zero_when_pending_or_in_handler)
{
    reset();
    timer_regs[TIME_TIMER_VT_HI] = 0x12;
    timer_regs[TIME_TIMER_VT_LO] = 0x34;
    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_VT_INT;
    ASSERT_EQ(0, TIME_$VT_TIMER());

    timer_regs[TIME_TIMER_CTRL] = TIME_CTRL_RTE_INT;   /* the other bit */
    ASSERT_EQ(0x1234, TIME_$VT_TIMER());

    IN_VT_INT = 1;
    ASSERT_EQ(0, TIME_$VT_TIMER());
}

TEST(wrt_timer_movep_and_flags)
{
    uint16_t idx, val;

    reset();
    IN_VT_INT = 0xFF; IN_RT_INT = 0xFF;

    idx = 1; val = 0x1046;                          /* real-time: no flag */
    TIME_$WRT_TIMER(&idx, &val);
    ASSERT_EQ(2, write_count);
    ASSERT_EQ(5, write_off[0]); ASSERT_EQ(0x10, write_val[0]);
    ASSERT_EQ(7, write_off[1]); ASSERT_EQ(0x46, write_val[1]);
    ASSERT_EQ(0xFF, IN_VT_INT);
    ASSERT_EQ(0xFF, IN_RT_INT);

    idx = 2; val = 0xFFFF;                          /* virtual: clears IN_VT_INT */
    TIME_$WRT_TIMER(&idx, &val);
    ASSERT_EQ(9, write_off[2]); ASSERT_EQ(11, write_off[3]);
    ASSERT_EQ(0, IN_VT_INT);
    ASSERT_EQ(0xFF, IN_RT_INT);

    idx = 3; val = 0xABCD;                          /* aux: clears IN_RT_INT */
    TIME_$WRT_TIMER(&idx, &val);
    ASSERT_EQ(13, write_off[4]); ASSERT_EQ(0xAB, write_val[4]);
    ASSERT_EQ(15, write_off[5]); ASSERT_EQ(0xCD, write_val[5]);
    ASSERT_EQ(0, IN_RT_INT);

    IN_VT_INT = 0xFF; IN_RT_INT = 0xFF;
    idx = 0; val = 1;                               /* control: no flag */
    TIME_$WRT_TIMER(&idx, &val);
    ASSERT_EQ(1, write_off[6]); ASSERT_EQ(3, write_off[7]);
    ASSERT_EQ(0xFF, IN_VT_INT);
    ASSERT_EQ(0xFF, IN_RT_INT);
}

TEST(set_vector_installs_level_6)
{
    reset();
    TIME_$SET_VECTOR();
    ASSERT_EQ((uintptr_t)&TIME_$TIMER_HANDLER, (uintptr_t)arch_$vector_table[30]);
    ASSERT_EQ((uintptr_t)&TIME_$TIMER_HANDLER, (uintptr_t)ARCH_VECTOR(0x78 / 4));
    ASSERT_EQ(0, (uintptr_t)arch_$vector_table[29]);
    ASSERT_EQ(0, (uintptr_t)arch_$vector_table[31]);
}

TEST(vt_int_scans_current_queue_then_clears_flag)
{
    reset();
    IN_VT_INT = 0xFF;
    PROC1_$CURRENT = 5;

    TIME_$VT_INT();

    ASSERT_EQ(1, scan_calls);
    ASSERT_EQ((uintptr_t)&TIME_$VTQ[4], (uintptr_t)scan_queue);
    ASSERT_EQ(0x1234, scan_now.high);
    ASSERT_EQ(0x5678, scan_now.low);
    ASSERT_EQ(0xFF, in_vt_int_at_scan);
    ASSERT_EQ(0, IN_VT_INT);
}

int main(void)
{
    printf("test_timer_regs:\n");
    RUN_TEST(vt_timer_reads_counter);
    RUN_TEST(vt_timer_zero_when_pending_or_in_handler);
    RUN_TEST(wrt_timer_movep_and_flags);
    RUN_TEST(set_vector_installs_level_6);
    RUN_TEST(vt_int_scans_current_queue_then_clears_flag);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
