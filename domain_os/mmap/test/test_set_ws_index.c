/*
 * mmap/test/test_set_ws_index.c - Unit tests for MMAP_$SET_WS_INDEX
 * (0x00E0D1C8)
 *
 * The free-slot scan is `dbf` on 0x3D from slot 8: exactly slots 8..69
 * (0x00E0D1F4-0x00E0D21A); the old loop ran one slot past MMAP_$WSL.
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
const status_$t mmap_$illegal_wsl_index_00e0c9e0 = status_$mmap_illegal_wsl_index;

static int crash_calls;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
}

#include "../set_ws_index.c"

static void reset_module(void)
{
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    crash_calls = 0;
    MMAP_$WSL_HI_MARK = 8;
}

TEST(allocates_last_slot_and_raises_hi_mark)
{
    uint16_t idx = 0;
    int i;

    reset_module();
    for (i = 8; i <= 68; i++) MMAP_$WSL[i].flags = WSL_FLAG_IN_USE;

    MMAP_$SET_WS_INDEX(3, &idx);

    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(69, idx);
    ASSERT_EQ(69, MMAP_$WSL_HI_MARK);
    ASSERT_EQ(WSL_FLAG_IN_USE, MMAP_$WSL[69].flags);
    ASSERT_EQ(69, MMAP_PID_TO_WSL[3]);
}

TEST(allocates_first_free_slot_without_touching_hi_mark)
{
    uint16_t idx = 0;

    reset_module();
    MMAP_$WSL_HI_MARK = 20;
    MMAP_$WSL[8].flags = WSL_FLAG_IN_USE;

    MMAP_$SET_WS_INDEX(3, &idx);

    ASSERT_EQ(9, idx);
    ASSERT_EQ(20, MMAP_$WSL_HI_MARK);
    ASSERT_EQ(9, MMAP_PID_TO_WSL[3]);
}

TEST(all_slots_used_crashes_exhausted)
{
    uint16_t idx = 0;
    int i;

    reset_module();
    for (i = 8; i <= 69; i++) MMAP_$WSL[i].flags = WSL_FLAG_IN_USE;
    MMAP_PID_TO_WSL[3] = 0x55;

    MMAP_$SET_WS_INDEX(3, &idx);

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(status_$mmap_ws_lists_exhausted, crash_status);
    ASSERT_EQ(0, idx);
    ASSERT_EQ(0x55, MMAP_PID_TO_WSL[3]);   /* the exit skips the store */
}

TEST(given_index_is_validated)
{
    uint16_t idx = 7;

    reset_module();
    MMAP_$WSL_HI_MARK = 8;

    MMAP_$SET_WS_INDEX(3, &idx);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(WSL_FLAG_IN_USE, MMAP_$WSL[7].flags);
    ASSERT_EQ(7, MMAP_PID_TO_WSL[3]);

    idx = 4;
    MMAP_$SET_WS_INDEX(3, &idx);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(status_$mmap_illegal_wsl_index, crash_status);

    idx = 9;   /* above the hi mark */
    MMAP_$SET_WS_INDEX(3, &idx);
    ASSERT_EQ(2, crash_calls);
}

TEST(bad_pid_crashes)
{
    uint16_t idx = 7;

    reset_module();

    MMAP_$SET_WS_INDEX(0x41, &idx);

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(status_$mmap_illegal_pid, crash_status);
}

int main(void)
{
    printf("MMAP_$SET_WS_INDEX tests\n");
    RUN_TEST(allocates_last_slot_and_raises_hi_mark);
    RUN_TEST(allocates_first_free_slot_without_touching_hi_mark);
    RUN_TEST(all_slots_used_crashes_exhausted);
    RUN_TEST(given_index_is_validated);
    RUN_TEST(bad_pid_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
