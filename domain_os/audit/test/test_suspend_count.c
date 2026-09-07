/*
 * audit/test/test_suspend_count.c - Unit tests for the per-process audit
 * suspension counters (bead source-wzs5).
 *
 * AUDIT_$DATA.suspend_count is a Pascal 1-based array: every access in the
 * image is `(-0x2,A5,PROC1_$CURRENT*2)` (0x00E70DC8, 0x00E70DE8, 0x00E70DA8,
 * 0x00E716B8), i.e. element (pid - 1).  These tests compile the real
 * audit/suspend.c, audit/resume.c, audit/is_process_audited.c and
 * audit/inherit_audit.c and check that PID 1 lands in element 0 and that PID
 * 64 - the last the 64-word array covers - stays in bounds.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    test_##name();                                                            \
    tests_passed++;                                                           \
    printf("done\n");                                                         \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    long long _e = (long long)(expected);                                     \
    long long _a = (long long)(actual);                                       \
    if (_e != _a) {                                                           \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n",          \
               _e, _a, __LINE__);                                             \
        tests_failed++;                                                       \
        return;                                                               \
    }                                                                         \
} while (0)

#include "audit/audit_internal.h"

/* Globals the code under test links against. */
audit_data_t AUDIT_$DATA;
uint16_t PROC1_$CURRENT;

#include "../suspend.c"
#include "../resume.c"
#include "../is_process_audited.c"
#include "../inherit_audit.c"

static void reset(void)
{
    memset(&AUDIT_$DATA, 0, sizeof(AUDIT_$DATA));
}

/* 0x00E70DC8: PID 1 must land in element 0, not element 1. */
TEST(suspend_pid_1_is_element_0)
{
    reset();
    PROC1_$CURRENT = 1;

    AUDIT_$SUSPEND();

    ASSERT_EQ(1, AUDIT_$DATA.suspend_count[0]);
    ASSERT_EQ(0, AUDIT_$DATA.suspend_count[1]);
}

/* The 64-word array covers PIDs 1..64; PID 64 is the last element. */
TEST(suspend_pid_64_is_last_element)
{
    reset();
    PROC1_$CURRENT = AUDIT_MAX_PROCESSES;   /* 64 */

    AUDIT_$SUSPEND();

    ASSERT_EQ(1, AUDIT_$DATA.suspend_count[AUDIT_MAX_PROCESSES - 1]);
    /* log_file_uid follows the array at 0x80; it must not have been touched */
    ASSERT_EQ(0, AUDIT_$DATA.log_file_uid.high);
}

TEST(suspend_and_resume_nest)
{
    reset();
    PROC1_$CURRENT = 7;

    AUDIT_$SUSPEND();
    AUDIT_$SUSPEND();
    ASSERT_EQ(2, AUDIT_$DATA.suspend_count[6]);

    AUDIT_$RESUME();
    ASSERT_EQ(1, AUDIT_$DATA.suspend_count[6]);

    AUDIT_$RESUME();
    ASSERT_EQ(0, AUDIT_$DATA.suspend_count[6]);
}

/* 0x00E70DAC: seq, so the boolean is 0xFF when the counter is zero. */
TEST(is_process_audited_tracks_the_same_element)
{
    reset();
    PROC1_$CURRENT = 3;

    ASSERT_EQ((int8_t)-1, AUDIT_$IS_PROCESS_AUDITED());

    AUDIT_$DATA.suspend_count[2] = 1;
    ASSERT_EQ(0, AUDIT_$IS_PROCESS_AUDITED());

    /* the neighbouring elements must not affect the answer */
    AUDIT_$DATA.suspend_count[2] = 0;
    AUDIT_$DATA.suspend_count[3] = 5;
    ASSERT_EQ((int8_t)-1, AUDIT_$IS_PROCESS_AUDITED());
}

/* 0x00E716B8: move.w (-0x2,A5,parent*2),(-0x2,A5,child*2) */
TEST(inherit_copies_parent_element_to_child_element)
{
    int16_t child = 12;
    status_$t status = -1;

    reset();
    PROC1_$CURRENT = 5;
    AUDIT_$DATA.suspend_count[4] = 3;

    AUDIT_$INHERIT_AUDIT(&child, &status);

    ASSERT_EQ(3, AUDIT_$DATA.suspend_count[11]);
    ASSERT_EQ(0, AUDIT_$DATA.suspend_count[12]);
    ASSERT_EQ(status_$ok, status);
}

int main(void)
{
    printf("=== AUDIT suspension counter tests ===\n");

    RUN_TEST(suspend_pid_1_is_element_0);
    RUN_TEST(suspend_pid_64_is_last_element);
    RUN_TEST(suspend_and_resume_nest);
    RUN_TEST(is_process_audited_tracks_the_same_element);
    RUN_TEST(inherit_copies_parent_element_to_child_element);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}
