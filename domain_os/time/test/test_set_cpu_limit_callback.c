/*
 * time/test/test_set_cpu_limit_callback.c - Unit tests for
 * TIME_$SET_CPU_LIMIT_CALLBACK (0x00E58AF8)
 *
 * The real time/set_cpu_limit_callback.c is #included; PROC2_$SIGNAL_OS is
 * mocked.  TIME_$CPU_LIMIT_DB is a host arena reached through
 * ARCH_HOST_VA_BASE, as time/test/test_itimer.c does for the itimer table.
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

MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);

static int signal_calls;
static uid_t *signal_uid;
static int16_t signal_number;
static uint32_t signal_param;

void PROC2_$SIGNAL_OS(uid_t *proc_uid, int16_t *signal, uint32_t *param,
                      status_$t *status_ret)
{
    signal_calls++;
    signal_uid = proc_uid;
    signal_number = *signal;
    signal_param = *param;
    *status_ret = status_$ok;
}

#include "../set_cpu_limit_callback.c"

static uint8_t cpu_arena[PROC2_UID_TABLE_SIZE * CPU_LIMIT_DB_ENTRY_SIZE];

static time_queue_elem_t *entry_for(uint16_t as_id)
{
    return (time_queue_elem_t *)(cpu_arena + as_id * CPU_LIMIT_DB_ENTRY_SIZE);
}

static uint32_t data_word;
static uint32_t *data_ptr;

static void reset(void)
{
    memset(cpu_arena, 0, sizeof(cpu_arena));
    ARCH_HOST_VA_BASE = (uintptr_t)cpu_arena - CPU_LIMIT_DB_BASE;
    signal_calls = 0;
    data_ptr = &data_word;
}

TEST(signals_when_expiry_set)
{
    reset();
    data_word = 0xFFFF0004;             /* low word is the as_id */
    entry_for(4)->expire_low = 1;

    TIME_$SET_CPU_LIMIT_CALLBACK((time_$callback_arg_t)&data_ptr);

    ASSERT_EQ(1, signal_calls);
    ASSERT_EQ((uintptr_t)&PROC2_$UNWIRED_DATA.uid[4], (uintptr_t)signal_uid);
    ASSERT_EQ(0x001B, signal_number);
    ASSERT_EQ(0x000D000B, signal_param);
}

TEST(silent_when_only_interval_set)
{
    reset();
    data_word = 4;
    entry_for(4)->interval_high = 0x55;
    entry_for(4)->interval_low = 0x66;
    entry_for(3)->expire_high = 0x77;   /* a neighbour must not count */

    TIME_$SET_CPU_LIMIT_CALLBACK((time_$callback_arg_t)&data_ptr);

    ASSERT_EQ(0, signal_calls);
}

TEST(constant_cells)
{
    reset();
    ASSERT_EQ(0x001B, time_$c_cpu_limit_signal);
    ASSERT_EQ(0x000D000B, time_$c_cpu_limit_fault);
    ASSERT_EQ(2, sizeof(time_$c_cpu_limit_signal));
    ASSERT_EQ(4, sizeof(time_$c_cpu_limit_fault));
}

int main(void)
{
    printf("test_set_cpu_limit_callback:\n");
    RUN_TEST(signals_when_expiry_set);
    RUN_TEST(silent_when_only_interval_set);
    RUN_TEST(constant_cells);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
