/*
 * xpd/test/test_ptrace_opts.c - XPD_$SET/INQ/RESET_PTRACE_OPTS,
 * XPD_$INHERIT_PTRACE_OPTIONS (ptrace_opts.c) and XPD_$FIND_INDEX
 * (find_index.c).
 */

#include <stdio.h>
#include <string.h>

#include "xpd/xpd_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-48s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
static proc2_info_t mock_entries[8];
proc2_info_t *P2_INFO_TABLE = mock_entries;
static uint16_t pid_to_index[64];
uint16_t *PROC2_$PID_TO_INDEX = pid_to_index;
uid_t UID_$NIL = { 0, 0 };

static int lock_held;
void ML_$LOCK(int16_t id)   { (void)id; lock_held++; }
void ML_$UNLOCK(int16_t id) { (void)id; lock_held--; }

static int16_t find_index_result;
static status_$t find_index_status;
int16_t PROC2_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret)
{
    (void)proc_uid;
    *status_ret = find_index_status;
    return find_index_result;
}

#include "../xpd_data.c"
#include "../ptrace_opts.c"
#include "../find_index.c"

static void reset(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(pid_to_index, 0, sizeof(pid_to_index));
    PROC1_$CURRENT = 3;
    pid_to_index[3] = 2;            /* the current process is index 2 */
    mock_entries[1].self_index = 2;
    mock_entries[3].self_index = 4;
    mock_entries[3].debugger_idx = 2;
    mock_entries[4].self_index = 5;
    mock_entries[4].debugger_idx = 6;
    find_index_result = 4;
    find_index_status = status_$ok;
    lock_held = 0;
}

static const xpd_$ptrace_opts_t sample = { 0x11223344u, 0x1000, 0x2000, 0xA5, 0x18 };

TEST(set_on_self_via_nil)
{
    uid_t nil = { 0, 0 };
    xpd_$ptrace_opts_t o = sample;
    status_$t st = 0x55;
    reset();
    XPD_$SET_PTRACE_OPTS(&nil, &o, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, memcmp(mock_entries[1].ptrace_opts, &sample, 14));
    ASSERT_EQ(0, lock_held);
}

TEST(set_on_target_as_debugger)
{
    uid_t u = { 1, 2 };
    xpd_$ptrace_opts_t o = sample;
    status_$t st = 0x55;
    reset();
    XPD_$SET_PTRACE_OPTS(&u, &o, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, memcmp(mock_entries[3].ptrace_opts, &sample, 14));
    /* neither self nor debugger */
    find_index_result = 5;
    XPD_$SET_PTRACE_OPTS(&u, &o, &st);
    ASSERT_EQ(status_$proc2_proc_not_debug_target, st);
    ASSERT_EQ(0, mock_entries[4].ptrace_opts[0]);
    /* the lookup fails: its status, nothing written */
    find_index_status = 0x00190001;
    XPD_$SET_PTRACE_OPTS(&u, &o, &st);
    ASSERT_EQ(0x00190001, st);
    ASSERT_EQ(0, lock_held);
}

TEST(inq)
{
    uid_t u = { 1, 2 };
    xpd_$ptrace_opts_t o;
    status_$t st = 0x55;
    reset();
    memset(&o, 0xEE, sizeof(o));
    memcpy(mock_entries[3].ptrace_opts, &sample, 14);
    XPD_$INQ_PTRACE_OPTS(&u, &o, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, memcmp(&o, &sample, 14));
    /* a failure leaves *opts alone */
    memset(&o, 0xEE, sizeof(o));
    find_index_result = 5;
    XPD_$INQ_PTRACE_OPTS(&u, &o, &st);
    ASSERT_EQ(status_$proc2_proc_not_debug_target, st);
    ASSERT_EQ(0xEE, (uint8_t)o.flags);
    ASSERT_EQ(0, lock_held);
}

TEST(reset_and_inherit)
{
    xpd_$ptrace_opts_t o = sample;
    ASSERT_EQ(-1, XPD_$INHERIT_PTRACE_OPTIONS(&o));
    o.flags2 = 0xF7;
    ASSERT_EQ(0, XPD_$INHERIT_PTRACE_OPTIONS(&o));
    XPD_$RESET_PTRACE_OPTS(&o);
    ASSERT_EQ(0, o.signal_mask); ASSERT_EQ(0, o.trace_range_lo);
    ASSERT_EQ(0, o.trace_range_hi); ASSERT_EQ(0, o.flags); ASSERT_EQ(0, o.flags2);
}

TEST(find_index)
{
    uid_t u = { 1, 2 };
    status_$t st = 0x55;
    reset();
    mock_entries[3].flags = XPD_PF_SUSPENDED;
    ASSERT_EQ(4, XPD_$FIND_INDEX(&u, &st));
    ASSERT_EQ(status_$ok, st);
    mock_entries[3].flags = 0;
    ASSERT_EQ(4, XPD_$FIND_INDEX(&u, &st));
    ASSERT_EQ(status_$xpd_target_not_suspended, st);
    find_index_result = 5;              /* debugger is 6, not us */
    ASSERT_EQ(5, XPD_$FIND_INDEX(&u, &st));
    ASSERT_EQ(status_$proc2_proc_not_debug_target, st);
    find_index_status = 0x00190001;
    find_index_result = 0;
    ASSERT_EQ(0, XPD_$FIND_INDEX(&u, &st));
    ASSERT_EQ(0x00190001, st);
}

int main(void)
{
    printf("XPD ptrace option tests\n");
    RUN_TEST(set_on_self_via_nil);
    RUN_TEST(set_on_target_as_debugger);
    RUN_TEST(inq);
    RUN_TEST(reset_and_inherit);
    RUN_TEST(find_index);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
