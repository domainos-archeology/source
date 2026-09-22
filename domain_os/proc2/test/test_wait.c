/*
 * Tests for PROC2_$WAIT (0x00E3FDD0) and its helpers WAIT_REAP_CHILD
 * (0x00E3FB34), WAIT_TRY_LIVE_CHILD (0x00E3FC5C), WAIT_TRY_ZOMBIE
 * (0x00E3FD06), plus PROC2_$UPID_TO_UID (0x00E40ECE), PROC2_$WHO_AM_I
 * (0x00E73862) and PROC2_$ZOMBIE_LIST (0x00E40548).
 *
 * Pinned by the disassembly: the pid range check (0x41..30000); the
 * +0x18/+0x1A match on the child walk; the selector chain; the result
 * record is copied out only on success; WNOHANG returns 0; a quit wake
 * (EC_$WAITN == 2) reports 0x180003 and resyncs FIM_$QUIT_VALUE; REAP
 * unlinks from the parent only when flags bit 15 is clear, clears 0x2000,
 * and lays the record out as documented; TRY_ZOMBIE's three arms.
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

#define MOCK_ENTRIES 8
static proc2_info_t mock_entries[MOCK_ENTRIES + 1];
static uint16_t mock_pid_to_index[64];
static pgroup_entry_t mock_pgroups[PGROUP_TABLE_SIZE];
proc2_info_t *P2_INFO_TABLE = &mock_entries[1];
uint16_t P2_INFO_ALLOC_PTR, P2_FREE_LIST_HEAD;
uint16_t *PROC2_$PID_TO_INDEX = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
proc2_ec_entry_t PROC2_$EC[PROC2_EC_ENTRIES];
uint16_t PROC1_$CURRENT;
uid_t UID_$NIL = { 0xAAAA5555u, 0x12345678u };
ec_$eventcount_t FIM_$QUIT_EC[8];
uint32_t FIM_$QUIT_VALUE[8];
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_waitn, n_clear, n_cleanup, n_find, n_rls, n_pop;
static uint16_t mock_waitn_result; static int16_t mock_find_by_upgid;
static int16_t last_clear_idx; static int8_t last_clear_flag; static int16_t last_cleanup_mode;
static status_$t mock_cleanup_status;
void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
int16_t PGROUP_FIND_BY_UPGID(uint16_t g) { (void)g; n_find++; return mock_find_by_upgid; }
void DEBUG_CLEAR_INTERNAL(int16_t i, int8_t f) { n_clear++; last_clear_idx = i; last_clear_flag = f; }
void PGROUP_CLEANUP_INTERNAL(proc2_info_t *e, int16_t m) { (void)e; n_cleanup++; last_cleanup_mode = m; }
int32_t EC_$READ(ec_$eventcount_t *ec) { return ec->value; }
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *vals, int16_t n)
{
    (void)ecs; (void)vals; (void)n; n_waitn++;
    if (mock_waitn_result != 2) { P2_INFO_ENTRY(3)->flags |= PROC2_FLAG_ZOMBIE; }  /* child exits */
    return mock_waitn_result;
}
status_$t FIM_$CLEANUP(void *h) { (void)h; return mock_cleanup_status; }
void FIM_$RLS_CLEANUP(void *h) { (void)h; n_rls++; }
void FIM_$POP_SIGNAL(void *h) { (void)h; n_pop++; }

#include "proc2/wait_reap_child.c"
#include "proc2/wait_try_live_child.c"
#include "proc2/wait_try_zombie.c"
#include "proc2/wait.c"
#include "proc2/upid_to_uid.c"
#include "proc2/who_am_i.c"
#include "proc2/zombie_list.c"

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
static proc2_wait_result_t res;
static void reset(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(&res, 0xEE, sizeof(res));
    memset(FIM_$QUIT_EC, 0, sizeof(FIM_$QUIT_EC)); memset(FIM_$QUIT_VALUE, 0, sizeof(FIM_$QUIT_VALUE));
    n_lock = n_unlock = n_waitn = n_clear = n_cleanup = n_find = n_rls = n_pop = 0;
    mock_waitn_result = 0; mock_find_by_upgid = 4; mock_cleanup_status = status_$cleanup_handler_set;
    PROC1_$CURRENT = 5; mock_pid_to_index[5] = 2;
    E(2)->self_index = 2; E(2)->asid = 3; E(2)->pad_18[0] = 7;
    /* child 3 of 2, upid 100, group 4 */
    E(2)->first_child_idx = 3; E(3)->pad_18[1] = 7; E(3)->upid = 100; E(3)->pgroup_table_idx = 4;
    E(3)->self_index = 3; E(3)->parent_pgroup_idx = 2;
    P2_INFO_ALLOC_PTR = 2; E(2)->next_index = 3; E(3)->pad_14 = 2; E(3)->next_index = 0;
    for (int i = 1; i <= MOCK_ENTRIES; i++) { E(i)->uid.high = 0x100 + i; E(i)->uid.low = i; }
}

TEST(wait_pid_range_and_no_children)
{
    uint16_t opt = 0; int16_t pid; status_$t st;
    pid = 30001; ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), -1);
    ASSERT_EQ(st, status_$proc2_wait_found_no_children); ASSERT_EQ(n_lock, 0);
    pid = 0x40; ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), -1);
    E(2)->first_child_idx = 0; pid = -1;
    ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), -1);
    ASSERT_EQ(st, status_$proc2_wait_found_no_children); ASSERT_EQ(n_lock, 0);
    ASSERT_EQ(res.flag_64, 0);                      /* cleared on the caller's record */
}

TEST(wait_wnohang_and_selector_mismatch)
{
    uint16_t opt = 1; int16_t pid = 100; status_$t st;
    ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), 0);       /* qualified but not ready */
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_waitn, 0); ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 1);
    pid = 101; ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), -1);   /* nobody qualifies */
    ASSERT_EQ(st, status_$proc2_wait_found_no_children);
    pid = -4; ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), 0);     /* |pid| -> group 4 */
    ASSERT_EQ(n_find, 1);
    E(3)->pad_18[1] = 8; pid = -1;                          /* +0x1A mismatch: skipped */
    ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), -1);
    ASSERT_EQ(st, status_$proc2_wait_found_no_children);
}

TEST(wait_blocks_then_reaps)
{
    uint16_t opt = 0; int16_t pid = -1; status_$t st;
    P2_FREE_LIST_HEAD = 6; E(3)->tty_uid.high = 0x77; E(3)->zombie_pad[2] = 0xC0;
    ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), 100);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_waitn, 1); ASSERT_EQ(n_lock, 2); ASSERT_EQ(n_unlock, 2);
    ASSERT_EQ(E(2)->first_child_idx, 0);                   /* unlinked from the parent */
    ASSERT_EQ(E(2)->next_index, 0); ASSERT_EQ(P2_FREE_LIST_HEAD, 3); ASSERT_EQ(E(3)->next_index, 6);
    ASSERT_EQ(E(3)->flags & PROC2_FLAG_ZOMBIE, 0); ASSERT_EQ(n_cleanup, 1); ASSERT_EQ(last_cleanup_mode, 1);
    ASSERT_EQ(res.flag_65, (int8_t)0xFF); ASSERT_EQ(res.flag_66, (int8_t)0xFF);
    ASSERT_EQ(memcmp(res.entry_60, &E(3)->tty_uid, 8), 0);   /* bytes of +0x60.. */
    ASSERT_EQ(memcmp(res.entry_60 + 0x36, &E(3)->asid, 2), 0);   /* ... through +0x97 */
}

TEST(wait_quit_wakeup)
{
    uint16_t opt = 0; int16_t pid = -1; status_$t st;
    mock_waitn_result = 2; FIM_$QUIT_EC[3].value = 9;
    ASSERT_EQ(PROC2_$WAIT(&opt, &pid, &res, &st), -1);
    ASSERT_EQ(st, status_$ec2_async_fault_while_waiting); ASSERT_EQ(FIM_$QUIT_VALUE[3], 9);
    ASSERT_EQ(res.exit_status, 0xEEEEEEEEu);               /* record not copied */
}

TEST(try_live_child_stop_arm_and_debugger_gate)
{
    int8_t found = 0x55; int16_t p = 0;
    E(3)->flags = 0x0040; E(3)->pad_94 = 0x11;
    PROC2_$WAIT_TRY_LIVE_CHILD(3, 2, 2, 0, &found, &res, &p);
    ASSERT_EQ(found, (int8_t)0xFF); ASSERT_EQ(res.exit_status, 0x117Fu); ASSERT_EQ(p, 100);
    ASSERT_EQ(E(3)->flags, 0x0060);
    PROC2_$WAIT_TRY_LIVE_CHILD(3, 2, 2, 0, &found, &res, &p);   /* already reported */
    ASSERT_EQ(found, 0);
    E(3)->flags = PROC2_FLAG_ZOMBIE; E(3)->debugger_idx = 6;   /* debugged by someone else */
    PROC2_$WAIT_TRY_LIVE_CHILD(3, 2, 2, 0, &found, &res, &p);
    ASSERT_EQ(found, 0); ASSERT_EQ(n_cleanup, 0);
    E(3)->debugger_idx = 2;
    PROC2_$WAIT_TRY_LIVE_CHILD(3, 2, 2, 0, &found, &res, &p);
    ASSERT_EQ(found, (int8_t)0xFF); ASSERT_EQ(n_cleanup, 1); ASSERT_EQ(n_clear, 1); ASSERT_EQ(last_clear_flag, 0);
}

TEST(try_zombie_arms)
{
    int8_t found = 0x55; int16_t p = 0;
    PROC2_$WAIT_TRY_ZOMBIE(3, 0, &found, &res, &p);        /* neither zombie nor stopped */
    ASSERT_EQ(found, 0);
    E(3)->flags = 0x0010; E(3)->pad_94 = 2; E(3)->fault_param[1] = 0x80; E(3)->fault_param[3] = 0x5A;
    PROC2_$WAIT_TRY_ZOMBIE(3, 0, &found, &res, &p);        /* stop arm */
    ASSERT_EQ(found, (int8_t)0xFF); ASSERT_EQ(E(3)->flags, 0x0030);
    ASSERT_EQ(res.exit_status, 0x27Fu); ASSERT_EQ(res.exit_info, 0x0000005Au);
    ASSERT_EQ(res.flag_64, (int8_t)0xFF); ASSERT_EQ(res.child_uid.high, 0x103); ASSERT_EQ(p, 100);
    E(3)->flags = PROC2_FLAG_ZOMBIE; E(3)->asid_alt = 0x1234; E(3)->level1_pid = 0x5678;
    PROC2_$WAIT_TRY_ZOMBIE(3, 0, &found, &res, &p);        /* plain zombie */
    ASSERT_EQ(n_clear, 1); ASSERT_EQ(n_cleanup, 0);
    ASSERT_EQ(res.exit_status, 0x12345678u); ASSERT_EQ(res.parent_uid.high, 0xAAAA5555u);
    E(3)->flags = PROC2_FLAG_ZOMBIE | 0x8000;
    PROC2_$WAIT_TRY_ZOMBIE(3, 0, &found, &res, &p);        /* orphan zombie: reaped, no unlink */
    ASSERT_EQ(n_cleanup, 1); ASSERT_EQ(E(2)->first_child_idx, 3);
}

TEST(upid_to_uid_and_who_am_i)
{
    int16_t u = 100; uid_t out; status_$t st;
    PROC2_$UPID_TO_UID(&u, &out, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(out.high, 0x103);
    E(3)->flags = PROC2_FLAG_ZOMBIE; PROC2_$UPID_TO_UID(&u, &out, &st);
    ASSERT_EQ(st, status_$proc2_zombie);
    u = 101; PROC2_$UPID_TO_UID(&u, &out, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found);
    PROC2_$WHO_AM_I(&out); ASSERT_EQ(out.high, 0x102); ASSERT_EQ(out.low, 2);
    ASSERT_EQ(n_lock, n_unlock);
}

TEST(zombie_list)
{
    uid_t out[4]; uint16_t max = 2, cnt; int32_t start = 1, last = 9; int8_t more = 0x55;
    E(2)->flags = 0x2100; E(3)->flags = 0x2100; E(5)->flags = 0x2100; E(6)->flags = 0x2000;
    PROC2_$ZOMBIE_LIST(out, &max, &cnt, &start, &more, &last);
    ASSERT_EQ(cnt, 2); ASSERT_EQ(out[0].high, 0x102); ASSERT_EQ(out[1].high, 0x103);
    ASSERT_EQ(more, (int8_t)0xFF); ASSERT_EQ(last, 4); ASSERT_EQ(n_rls, 1);
    mock_cleanup_status = 1; cnt = 7;
    PROC2_$ZOMBIE_LIST(out, &max, &cnt, &start, &more, &last);
    ASSERT_EQ(cnt, 0); ASSERT_EQ(n_pop, 1);
}

int main(void)
{
    RUN_TEST(wait_pid_range_and_no_children);
    RUN_TEST(wait_wnohang_and_selector_mismatch);
    RUN_TEST(wait_blocks_then_reaps);
    RUN_TEST(wait_quit_wakeup);
    RUN_TEST(try_live_child_stop_arm_and_debugger_gate);
    RUN_TEST(try_zombie_arms);
    RUN_TEST(upid_to_uid_and_who_am_i);
    RUN_TEST(zombie_list);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
