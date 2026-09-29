/*
 * mmap/test/test_free_wsl.c - Unit tests for MMAP_$FREE_WSL (0x00E0D158)
 *
 * The sharing scan at 0x00E0D18C starts at A5+2 and runs 64 words, i.e.
 * MMAP_PID_TO_WSL[1..64]; entry 0 (MMAP_$WSL_HI_MARK) is not compared.
 */

#include <stdio.h>
#include <string.h>

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
#include "arch/arch.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);
const status_$t mmap_$illegal_pid_00e0d1c4 = status_$mmap_illegal_pid;

static int purge_calls, crash_calls;
static uint16_t purge_index;
static status_$t crash_status;

void MMAP_$PURGE(uint16_t wsl_index)
{
    purge_calls++;
    purge_index = wsl_index;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
}

#include "../free_wsl.c"

static void reset_module(void)
{
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    purge_calls = crash_calls = 0;
    MMAP_$WSL[7].flags = 0xFF;
    MMAP_PID_TO_WSL[3] = 7;
}

TEST(sole_user_purges_and_frees_slot)
{
    reset_module();
    MMAP_$WSL_HI_MARK = 7;   /* entry 0 must not count as a sharer */

    MMAP_$FREE_WSL(3);

    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(0, MMAP_PID_TO_WSL[3]);
    ASSERT_EQ(1, purge_calls);
    ASSERT_EQ(7, purge_index);
    ASSERT_EQ(0x7F, MMAP_$WSL[7].flags);
}

TEST(shared_slot_is_kept)
{
    reset_module();
    MMAP_PID_TO_WSL[64] = 7;   /* last scanned entry */

    MMAP_$FREE_WSL(3);

    ASSERT_EQ(0, MMAP_PID_TO_WSL[3]);
    ASSERT_EQ(0, purge_calls);
    ASSERT_EQ(0xFF, MMAP_$WSL[7].flags);
}

TEST(bad_pid_crashes)
{
    reset_module();
    MMAP_PID_TO_WSL[0x41] = 9;

    MMAP_$FREE_WSL(0x41);

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(status_$mmap_illegal_pid, crash_status);
}

int main(void)
{
    printf("MMAP_$FREE_WSL tests\n");
    RUN_TEST(sole_user_purges_and_frees_slot);
    RUN_TEST(shared_slot_is_kept);
    RUN_TEST(bad_pid_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
