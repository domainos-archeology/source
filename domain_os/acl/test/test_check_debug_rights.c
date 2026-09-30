/*
 * acl/test/test_check_debug_rights.c - unit tests for ACL_$CHECK_DEBUG_RIGHTS (0x00E48ADA).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "acl/acl_internal.h"

MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);
uid_t UID_$NIL = { 0, 0 };

static int8_t suser_ret, saved_ret, orig_ret;
static int n_sids;
static int16_t sids_pid[2];
static acl_sid_block_t *sids_arg[2];

int8_t acl_$check_suser_pid(int16_t pid) { (void)pid; return suser_ret; }
int8_t acl_$sids_allowed(acl_sid_block_t *sids, int16_t pid)
{
    int i = n_sids++;
    sids_arg[i] = sids; sids_pid[i] = pid;
    return i == 0 ? saved_ret : orig_ret;
}

#include "../check_debug_rights.c"

#define P1 4
#define P2 7

static void setp(int pid, int idx, uint32_t v)
{
    ACL_$DATA.proj_uids[pid][idx].high = v;
    ACL_$DATA.proj_uids[pid][idx].low = v ? 0x99 : 0;
}

static int8_t run(void)
{
    int16_t a = P1, b = P2;
    n_sids = 0;
    return ACL_$CHECK_DEBUG_RIGHTS(&a, &b);
}

static void reset(void)
{
    memset(&ACL_$DATA, 0, sizeof(ACL_$DATA));
    suser_ret = 0; saved_ret = -1; orig_ret = -1;
}

TEST(sids_checked_saved_then_original)
{
    reset();
    ASSERT_EQ(0xFF, (uint8_t)run());      /* both project lists empty */
    ASSERT_EQ(2, n_sids);
    ASSERT_EQ((uintptr_t)&ACL_$DATA.saved_sids[P2], (uintptr_t)sids_arg[0]);
    ASSERT_EQ((uintptr_t)&ACL_$DATA.original_sids[P2], (uintptr_t)sids_arg[1]);
    ASSERT_EQ(P1, sids_pid[0]);

    reset(); saved_ret = 0;
    ASSERT_EQ(0, run());
    ASSERT_EQ(1, n_sids);
    reset(); orig_ret = 0;
    ASSERT_EQ(0, run());
}

/* The super-user preset survives failing tests. */
TEST(suser_presets_result)
{
    reset(); suser_ret = -1; saved_ret = 0;
    ASSERT_EQ(0xFF, (uint8_t)run());
}

TEST(projects_subset)
{
    reset();
    setp(P2, 0, 0x10); setp(P2, 1, 0x20);
    setp(P1, 0, 0x20); setp(P1, 1, 0x30); setp(P1, 2, 0x10);
    ASSERT_EQ(0xFF, (uint8_t)run());

    setp(P2, 2, 0x40);                    /* not in pid1's list */
    ASSERT_EQ(0, run());

    setp(P1, 3, 0x40);                    /* now it is */
    ASSERT_EQ(0xFF, (uint8_t)run());

    setp(P1, 1, 0);                       /* NIL at pid1[1] hides [2], [3] */
    ASSERT_EQ(0, run());
}

TEST(full_lists)
{
    int i;
    reset();
    for (i = 0; i < 8; i++) { setp(P1, i, 0x100 + i); setp(P2, i, 0x107 - i); }
    ASSERT_EQ(0xFF, (uint8_t)run());
    setp(P2, 7, 0x999);                   /* ran off pid1's end: k = 9 */
    ASSERT_EQ(0, run());
}

int main(void)
{
    printf("ACL_$CHECK_DEBUG_RIGHTS tests\n");
    RUN_TEST(sids_checked_saved_then_original);
    RUN_TEST(suser_presets_result);
    RUN_TEST(projects_subset);
    RUN_TEST(full_lists);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
