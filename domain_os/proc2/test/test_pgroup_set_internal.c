/*
 * Tests for the process-group table routines:
 *   PGROUP_FIND_BY_UPGID (0x00E42224), PGROUP_DECR_LEADER_COUNT
 *   (0x00E42028), PGROUP_SET_INTERNAL (0x00E41E86),
 *   PROC2_$PGROUP_INHERIT_INTERNAL (0x00E4216E), PROC2_$PGROUP_INFO
 *   (0x00E41DBC).
 *
 * Pinned by the disassembly:
 *   - FIND scans slots 1..70 and compares a sign-extended argument with a
 *     zero-extended table word, so 0x8000.. never matches;
 *   - DECR signals 1 then 0x16 only when the count hits zero AND some
 *     member has flags bit 0x0040;
 *   - SET: session mismatch -> 0x190017 with nothing changed; the free-slot
 *     scan covers 1..70 and crashes with 0x190016 when full; the leader
 *     bookkeeping against the parent and the children;
 *   - INFO: upgid 0 answers (0, 0xFF, ok) without locking; the upid
 *     fallback; is_leader is leader_count == 0.
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);
MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_cleanup, n_crash, n_sig;
static int16_t sig_idx[4], sig_num[4]; static const status_$t *last_crash;
static int16_t last_cleanup_mode;
void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
void PGROUP_CLEANUP_INTERNAL(proc2_info_t *e, int16_t m) { (void)e; n_cleanup++; last_cleanup_mode = m; }
void CRASH_SYSTEM(const status_$t *s) { n_crash++; last_crash = s; }
void PROC2_$SIGNAL_PGROUP_INTERNAL(int16_t idx, int16_t sig, uint32_t p, int8_t chk, status_$t *s)
{ (void)p; (void)chk; if (n_sig < 4) { sig_idx[n_sig] = idx; sig_num[n_sig] = sig; } n_sig++; *s = status_$ok; }

#include "proc2/pgroup_find_by_upgid.c"
#include "proc2/pgroup_decr_leader_count.c"
#include "proc2/pgroup_set_internal.c"
#include "proc2/pgroup_inherit_internal.c"
#include "proc2/pgroup_info.c"

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
static pgroup_entry_t *G(int i) { return (&PROC2_$DATA.pgroup[i]); }
static void reset(void)
{
    memset(PROC2_$DATA.info, 0, sizeof(PROC2_$DATA.info));
    memset(PROC2_$DATA.pgroup, 0, sizeof(PROC2_$DATA.pgroup));
    n_lock = n_unlock = n_cleanup = n_crash = n_sig = 0; last_crash = NULL;
    PROC2_$UNWIRED_DATA.info_alloc_ptr = 0;
}

TEST(find_scans_1_to_70_and_sign_quirk)
{
    G(70)->ref_count = 1; G(70)->upgid = 500;
    G(3)->ref_count = 0; G(3)->upgid = 500;         /* free: skipped */
    ASSERT_EQ(PGROUP_FIND_BY_UPGID(500), 70);
    G(5)->ref_count = 1; G(5)->upgid = 0x9000;
    ASSERT_EQ(PGROUP_FIND_BY_UPGID(0x9000), 0);     /* sign vs zero extension */
    ASSERT_EQ(PGROUP_FIND_BY_UPGID(501), 0);
}

TEST(decr_signals_only_at_zero_with_flag_0x40)
{
    G(4)->leader_count = 2;
    PROC2_$UNWIRED_DATA.info_alloc_ptr = 2; E(2)->next_index = 0; E(2)->pgroup_table_idx = 4; E(2)->flags = 0x0040;
    PGROUP_DECR_LEADER_COUNT(4);
    ASSERT_EQ(G(4)->leader_count, 1); ASSERT_EQ(n_sig, 0);
    PGROUP_DECR_LEADER_COUNT(4);
    ASSERT_EQ(G(4)->leader_count, 0); ASSERT_EQ(n_sig, 2);
    ASSERT_EQ(sig_idx[0], 4); ASSERT_EQ(sig_num[0], 1); ASSERT_EQ(sig_num[1], 0x16);
    E(2)->flags = 0x4000; G(4)->leader_count = 1; n_sig = 0;
    PGROUP_DECR_LEADER_COUNT(4);
    ASSERT_EQ(n_sig, 0);                             /* high-byte bit is not it */
    PGROUP_DECR_LEADER_COUNT(0);
    ASSERT_EQ(G(0)->leader_count, 0);
}

TEST(set_zero_clears_via_cleanup)
{
    status_$t st = 5;
    E(3)->pgroup_table_idx = 4;
    PGROUP_SET_INTERNAL(E(3), 0, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_cleanup, 1); ASSERT_EQ(last_cleanup_mode, 2);
    ASSERT_EQ(E(3)->pgroup_table_idx, 0);
}

TEST(set_join_existing_session_mismatch)
{
    status_$t st = 0;
    G(4)->ref_count = 1; G(4)->upgid = 77; G(4)->session_id = 9;
    E(3)->session_id = 8; E(3)->pgroup_table_idx = 2; G(2)->ref_count = 1;
    PGROUP_SET_INTERNAL(E(3), 77, &st);
    ASSERT_EQ(st, status_$proc2_pgroup_in_different_session);
    ASSERT_EQ(G(4)->ref_count, 1); ASSERT_EQ(G(2)->ref_count, 1); ASSERT_EQ(E(3)->pgroup_table_idx, 2);
}

TEST(set_join_existing_with_leader_bookkeeping)
{
    status_$t st = 0;
    /* entry 3: session 9, old group 2, parent 1 (group 2), children 5 (group 2) and 6 (group 4) */
    G(2)->ref_count = 2; G(2)->leader_count = 1; G(2)->session_id = 9; G(2)->upgid = 20;
    G(4)->ref_count = 1; G(4)->leader_count = 1; G(4)->session_id = 9; G(4)->upgid = 40;
    E(3)->session_id = 9; E(3)->pgroup_table_idx = 2; E(3)->parent_pgroup_idx = 1; E(3)->first_child_idx = 5;
    E(1)->session_id = 9; E(1)->pgroup_table_idx = 2;
    E(5)->session_id = 9; E(5)->pgroup_table_idx = 2; E(5)->next_child_sibling = 6;
    E(6)->session_id = 9; E(6)->pgroup_table_idx = 4;
    PGROUP_SET_INTERNAL(E(3), 40, &st);
    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(E(3)->pgroup_table_idx, 4);
    ASSERT_EQ(G(4)->ref_count, 2); ASSERT_EQ(G(2)->ref_count, 1);
    /* parent in group 2: old == parent's -> no decrement; new != parent's -> G4 +1 */
    /* child 5 in old group 2 -> G2 +1; child 6 in new group 4 -> G4 -1 */
    ASSERT_EQ(G(2)->leader_count, 2);
    ASSERT_EQ(G(4)->leader_count, 1);
}

TEST(set_new_slot_and_table_full)
{
    status_$t st = 0;
    E(3)->session_id = 9;
    G(1)->ref_count = 1;
    PGROUP_SET_INTERNAL(E(3), 123, &st);
    ASSERT_EQ(E(3)->pgroup_table_idx, 2);
    ASSERT_EQ(G(2)->ref_count, 1); ASSERT_EQ(G(2)->leader_count, 0);
    ASSERT_EQ(G(2)->upgid, 123); ASSERT_EQ(G(2)->session_id, 9);
    for (int i = 1; i <= 70; i++) G(i)->ref_count = 1;
    E(4)->pgroup_table_idx = 5;
    PGROUP_SET_INTERNAL(E(4), 124, &st);
    ASSERT_EQ(n_crash, 1); ASSERT_EQ(*last_crash, status_$proc2_process_using_pgroup_id);
    ASSERT_EQ(E(4)->pgroup_table_idx, 0);
}

TEST(inherit)
{
    G(4)->ref_count = 1; E(2)->pgroup_table_idx = 4;
    PROC2_$PGROUP_INHERIT_INTERNAL(E(2), E(3));
    ASSERT_EQ(G(4)->ref_count, 2); ASSERT_EQ(E(3)->pgroup_table_idx, 4);
    E(5)->pgroup_table_idx = 0; E(3)->pgroup_table_idx = 4;
    PROC2_$PGROUP_INHERIT_INTERNAL(E(5), E(3));
    ASSERT_EQ(E(3)->pgroup_table_idx, 0); ASSERT_EQ(G(0)->ref_count, 0);
}

TEST(info_paths)
{
    uint16_t id = 0, sess = 5; uint8_t lead = 0; status_$t st = 1;
    PROC2_$PGROUP_INFO(&id, &sess, &lead, &st);
    ASSERT_EQ(sess, 0); ASSERT_EQ(lead, 0xFF); ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_lock, 0);
    G(4)->ref_count = 1; G(4)->upgid = 44; G(4)->session_id = 9; G(4)->leader_count = 1;
    id = 44; PROC2_$PGROUP_INFO(&id, &sess, &lead, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(sess, 9); ASSERT_EQ(lead, 0); ASSERT_EQ(n_lock, 1);
    /* fallback through a process upid */
    PROC2_$UNWIRED_DATA.info_alloc_ptr = 3; E(3)->upid = 45; E(3)->pgroup_table_idx = 4; G(4)->leader_count = 0;
    id = 45; PROC2_$PGROUP_INFO(&id, &sess, &lead, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(lead, 0xFF);
    id = 46; sess = 77; PROC2_$PGROUP_INFO(&id, &sess, &lead, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found); ASSERT_EQ(sess, 77);
    ASSERT_EQ(n_lock, n_unlock);
}

int main(void)
{
    RUN_TEST(find_scans_1_to_70_and_sign_quirk);
    RUN_TEST(decr_signals_only_at_zero_with_flag_0x40);
    RUN_TEST(set_zero_clears_via_cleanup);
    RUN_TEST(set_join_existing_session_mismatch);
    RUN_TEST(set_join_existing_with_leader_bookkeeping);
    RUN_TEST(set_new_slot_and_table_full);
    RUN_TEST(inherit);
    RUN_TEST(info_paths);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
