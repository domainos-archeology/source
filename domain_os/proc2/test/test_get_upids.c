/*
 * Tests for PROC2_$GET_UPIDS (0x00E738A8) and PROC2_$GET_MY_UPIDS
 * (0x00E73968).
 *
 * Pinned by the disassembly: the output order is (own upid, PARENT's upid
 * or 1, process-group upgid or 0) -- the parent comes from
 * P2[entry+0x1E]->upid (0x00E73912 / 0x00E739C4) and the group id from
 * PROC2_$DATA.pgroup[entry+0x10].upgid (0x00E73930 / 0x00E739E4).
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
uint16_t PROC1_$CURRENT;
int __host_intr_disable_count = 0;

static int n_lock, n_unlock;
static int16_t mock_find_index; static status_$t mock_find_status;
void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
int16_t PROC2_$FIND_INDEX(uid_t *u, status_$t *s) { (void)u; *s = mock_find_status; return mock_find_index; }

#include "proc2/get_upids.c"
#include "proc2/get_my_upids.c"

static int tests_run, tests_failed;
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { tests_run++; reset(); name(); } while (0)
#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("  FAIL %s:%d: %s == %lld, expected %s == %lld\n", \
               __FILE__, __LINE__, #a, _a, #b, _b); \
        tests_failed++; return; } } while (0)

static proc2_info_t *E(int i) { return P2_INFO_ENTRY(i); }
static void reset(void)
{
    memset(PROC2_$DATA.info, 0, sizeof(PROC2_$DATA.info));
    memset(PROC2_$DATA.pgroup, 0, sizeof(PROC2_$DATA.pgroup));
    n_lock = n_unlock = 0; mock_find_index = 3; mock_find_status = status_$ok;
    PROC1_$CURRENT = 5; PROC2_$DATA.pid_to_index[5] = 3;
    E(3)->upid = 300; E(3)->parent_pgroup_idx = 2; E(2)->upid = 200;
    E(3)->pgroup_table_idx = 4; PROC2_$DATA.pgroup[4].upgid = 400;
}

TEST(get_upids_success_order)
{
    uid_t u = { 1, 2 }; uint16_t a = 0, b = 0, c = 0; status_$t st = 1;
    PROC2_$GET_UPIDS(&u, &a, &b, &c, &st);
    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(a, 300); ASSERT_EQ(b, 200); ASSERT_EQ(c, 400);
    ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 1);
}

TEST(get_upids_defaults_no_parent_no_group)
{
    uid_t u = { 1, 2 }; uint16_t a, b, c; status_$t st;
    E(3)->parent_pgroup_idx = 0; E(3)->pgroup_table_idx = 0;
    PROC2_$GET_UPIDS(&u, &a, &b, &c, &st);
    ASSERT_EQ(b, 1); ASSERT_EQ(c, 0);
}

TEST(get_upids_failure_propagates_status)
{
    uid_t u = { 1, 2 }; uint16_t a, b, c; status_$t st = 0;
    mock_find_status = status_$proc2_uid_not_found;
    PROC2_$GET_UPIDS(&u, &a, &b, &c, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found);
    ASSERT_EQ(n_unlock, 1);
}

TEST(get_my_upids_order)
{
    uint16_t a = 0, b = 0, c = 0;
    PROC2_$GET_MY_UPIDS(&a, &b, &c);
    ASSERT_EQ(a, 300); ASSERT_EQ(b, 200); ASSERT_EQ(c, 400);
}

TEST(get_my_upids_defaults)
{
    uint16_t a, b, c;
    E(3)->parent_pgroup_idx = 0; E(3)->pgroup_table_idx = 0;
    PROC2_$GET_MY_UPIDS(&a, &b, &c);
    ASSERT_EQ(b, 1); ASSERT_EQ(c, 0);
}

int main(void)
{
    RUN_TEST(get_upids_success_order);
    RUN_TEST(get_upids_defaults_no_parent_no_group);
    RUN_TEST(get_upids_failure_propagates_status);
    RUN_TEST(get_my_upids_order);
    RUN_TEST(get_my_upids_defaults);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
