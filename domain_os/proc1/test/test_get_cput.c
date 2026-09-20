/*
 * Tests for proc1_$get_current_cpu_time (0x00E208D0), PROC1_$GET_CPUT
 * (0x00E20894), PROC1_$GET_CPUT8 (0x00E2089C) and PROC1_$GET_CPU_USAGE
 * (0x00E208AA).  Includes the real proc1/get_cput.c and
 * proc1/get_cpu_usage.c with TIME_$VT_TIMER mocked.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"
#include "time/time.h"

int __host_intr_disable_count = 0;

static proc1_t pcb;
proc1_t *PROC1_$CURRENT_PCB = &pcb;

static uint16_t vt_now;
static int ipl_at_vt;
static int n_vt;

uint16_t TIME_$VT_TIMER(void)
{
    n_vt++;
    ipl_at_vt = __host_intr_disable_count;
    return vt_now;
}

#include "../get_cput.c"
#include "../get_cpu_usage.c"

static int tests_run, tests_failed;

#define ASSERT_EQ(a, b) do {                                                  \
    unsigned long long _a = (unsigned long long)(a);                          \
    unsigned long long _b = (unsigned long long)(b);                          \
    if (_a != _b) {                                                           \
        printf("  FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", __FILE__,       \
               __LINE__, #a, #b, _a, _b);                                     \
        tests_failed++;                                                       \
    }                                                                         \
} while (0)

#define RUN_TEST(fn) do { tests_run++; reset(); fn(); } while (0)

static void reset(void)
{
    memset(&pcb, 0, sizeof(pcb));
    vt_now = 0;
    n_vt = 0;
    __host_intr_disable_count = 0;
}

/* 0x00E208E0..0x00E208EA: low = cpu_usage + (vtimer - now), no carry */
static void test_helper_plain(void)
{
    uint32_t hi; uint16_t lo;

    pcb.cpu_total = 0x12345678;
    pcb.cpu_usage = 0x1000;
    pcb.vtimer = 0x0300;
    vt_now = 0x0100;
    proc1_$get_current_cpu_time(&hi, &lo);

    ASSERT_EQ(hi, 0x12345678);
    ASSERT_EQ(lo, 0x1200);
    ASSERT_EQ(n_vt, 1);
    /* 0x00E208D2: the timer is read at IPL 7; 0x00E208F2 restores */
    ASSERT_EQ(ipl_at_vt, 1);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

/* 0x00E208EE / 0x00E208F0: a carry out of the word add bumps the high part */
static void test_helper_carry(void)
{
    uint32_t hi; uint16_t lo;

    pcb.cpu_total = 7;
    pcb.cpu_usage = 0xFFF0;
    pcb.vtimer = 0x0020;
    vt_now = 0x0000;
    proc1_$get_current_cpu_time(&hi, &lo);

    ASSERT_EQ(hi, 8);
    ASSERT_EQ(lo, 0x0010);
}

/* the vtimer difference is a word subtraction; wrap-around is intended */
static void test_helper_timer_wrapped(void)
{
    uint32_t hi; uint16_t lo;

    pcb.cpu_total = 0;
    pcb.cpu_usage = 0;
    pcb.vtimer = 0x0010;
    vt_now = 0x0020;
    proc1_$get_current_cpu_time(&hi, &lo);

    ASSERT_EQ(hi, 0);
    ASSERT_EQ(lo, 0xFFF0);
}

/* 0x00E2089C: GET_CPUT8 stores the unshifted 48-bit value */
static void test_get_cput8(void)
{
    clock_t c = { 0xAAAAAAAA, 0xAAAA };

    pcb.cpu_total = 0x80000001;
    pcb.cpu_usage = 0x8001;
    PROC1_$GET_CPUT8(&c);

    ASSERT_EQ(c.high, 0x80000001);
    ASSERT_EQ(c.low, 0x8001);
}

/*
 * 0x00E20896: lsl.w #1,D0 / roxl.l #1,D1 - bit 15 of the low word moves
 * into bit 0 of the high longword, bit 31 of the high longword is lost.
 */
static void test_get_cput_shift(void)
{
    clock_t c;

    pcb.cpu_total = 0x80000001;
    pcb.cpu_usage = 0x8001;
    PROC1_$GET_CPUT(&c);

    ASSERT_EQ(c.high, 0x00000003);
    ASSERT_EQ(c.low, 0x0002);

    reset();
    pcb.cpu_total = 0x00000001;
    pcb.cpu_usage = 0x7FFF;
    PROC1_$GET_CPUT(&c);
    ASSERT_EQ(c.high, 0x00000002);
    ASSERT_EQ(c.low, 0xFFFE);
}

/* 0x00E208AA..0x00E208CA: shifted time plus PCB+0x60 and PCB+0x64 */
static void test_get_cpu_usage(void)
{
    clock_t c;
    uint32_t s1 = 0, s2 = 0;

    pcb.cpu_total = 0x00000010;
    pcb.cpu_usage = 0x8000;
    pcb.field_60 = 0x60606060;
    pcb.field_64 = 0x64646464;
    PROC1_$GET_CPU_USAGE(&c, &s1, &s2);

    ASSERT_EQ(c.high, 0x00000021);
    ASSERT_EQ(c.low, 0x0000);
    ASSERT_EQ(s1, 0x60606060);
    ASSERT_EQ(s2, 0x64646464);
    ASSERT_EQ(__host_intr_disable_count, 0);
}

int main(void)
{
    RUN_TEST(test_helper_plain);
    RUN_TEST(test_helper_carry);
    RUN_TEST(test_helper_timer_wrapped);
    RUN_TEST(test_get_cput8);
    RUN_TEST(test_get_cput_shift);
    RUN_TEST(test_get_cpu_usage);
    printf("test_get_cput: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
