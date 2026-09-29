/*
 * Tests for the PROC2_$SET_* family:
 *   SET_PGROUP (0x00E410C8), SET_SESSION_ID (0x00E41C42), SET_SERVER
 *   (0x00E41468), SET_PRIORITY (0x00E414DE), SET_ACCT_INFO (0x00E41AC0),
 *   SET_CLEANUP (0x00E41572), SET_NAME (0x00E3EA2C).
 *
 * Pinned by the disassembly: the setpgid permission chain and its three
 * statuses; SET_SESSION_ID's leader / foreign-group / different-session
 * arms; SET_SERVER writes 0x0200 (high byte); SET_PRIORITY orders the
 * pair unsigned, tests the LOW status word and passes TRUE; SET_ACCT_INFO
 * clamps to 32, stores the length and clears 0x0008; SET_CLEANUP masks the
 * bit number mod 32; SET_NAME's bounds, the 0x22 marker and the low byte.
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
uint16_t PROC1_$CURRENT, PROC1_$AS_ID;
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_set_internal, n_cleanup, n_p1_set;
static int16_t mock_find_index; static status_$t mock_find_status, mock_set_status;
static uint16_t last_set_upgid; static proc2_info_t *last_set_entry;
static uint16_t last_p1_pid, last_p1_min, last_p1_max; static int8_t last_p1_mode;
void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
int16_t PROC2_$FIND_INDEX(uid_t *u, status_$t *s) { (void)u; *s = mock_find_status; return mock_find_index; }
void PGROUP_SET_INTERNAL(proc2_info_t *e, uint16_t g, status_$t *s)
{ n_set_internal++; last_set_entry = e; last_set_upgid = g; *s = mock_set_status; }
void PGROUP_CLEANUP_INTERNAL(proc2_info_t *e, int16_t m) { (void)e; (void)m; n_cleanup++; }
void PROC1_$SET_PRIORITY(uint16_t pid, int8_t mode, uint16_t *mn, uint16_t *mx)
{ n_p1_set++; last_p1_pid = pid; last_p1_mode = mode; last_p1_min = *mn; last_p1_max = *mx; }

#include "proc2/pgroup_find_by_upgid.c"
#include "proc2/set_pgroup.c"
#include "proc2/set_session_id.c"
#include "proc2/set_server.c"
#include "proc2/set_priority.c"
#include "proc2/set_acct_info.c"
#include "proc2/set_cleanup.c"
#include "proc2/set_name.c"

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
    n_lock = n_unlock = n_set_internal = n_cleanup = n_p1_set = 0;
    mock_find_index = 3; mock_find_status = status_$ok; mock_set_status = status_$ok;
    PROC1_$CURRENT = 5; PROC1_$AS_ID = 2; PROC2_$DATA.pid_to_index[5] = 2;
    E(2)->self_index = 2; E(2)->session_id = 9;
    E(3)->self_index = 3; E(3)->parent_pgroup_idx = 2; E(3)->session_id = 9; E(3)->upid = 30;
}

static uid_t U = { 1, 2 };

TEST(set_pgroup_permission_chain)
{
    uint16_t g = 0; status_$t st;
    /* not me, not my child */
    E(3)->parent_pgroup_idx = 4;
    PROC2_$SET_PGROUP(&U, &g, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found); ASSERT_EQ(n_set_internal, 0);
    /* my child, orphaned, not debugged */
    E(3)->parent_pgroup_idx = 2; E(3)->flags = PROC2_FLAG_ORPHAN;
    PROC2_$SET_PGROUP(&U, &g, &st);
    ASSERT_EQ(st, status_$proc2_permission_denied);
    /* orphaned but debugged, other session */
    E(3)->flags = PROC2_FLAG_ORPHAN | PROC2_FLAG_DEBUG; E(3)->session_id = 8;
    PROC2_$SET_PGROUP(&U, &g, &st);
    ASSERT_EQ(st, status_$proc2_pgroup_in_different_session);
    /* my child, same session, group 0 -> set and clear session */
    E(3)->flags = 0; E(3)->session_id = 9;
    PROC2_$SET_PGROUP(&U, &g, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_set_internal, 1); ASSERT_EQ(last_set_upgid, 0);
    ASSERT_EQ(E(3)->session_id, 0);
    ASSERT_EQ(n_lock, n_unlock);
}

TEST(set_pgroup_group_validation)
{
    uint16_t g; status_$t st;
    mock_find_index = 2;           /* target is me */
    E(2)->upid = 20; E(2)->session_id = 9;
    g = 77; PROC2_$SET_PGROUP(&U, &g, &st);         /* no such group */
    ASSERT_EQ(st, status_$proc2_pgroup_in_different_session);
    G(4)->ref_count = 1; G(4)->upgid = 77; G(4)->session_id = 8;
    PROC2_$SET_PGROUP(&U, &g, &st);                 /* wrong session */
    ASSERT_EQ(st, status_$proc2_pgroup_in_different_session);
    G(4)->session_id = 9;
    PROC2_$SET_PGROUP(&U, &g, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(last_set_upgid, 77); ASSERT_EQ(E(2)->session_id, 9);
    g = 20; PROC2_$SET_PGROUP(&U, &g, &st);         /* own upid: no lookup needed */
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_set_internal, 2);
    E(2)->session_id = 20;                          /* session leader */
    PROC2_$SET_PGROUP(&U, &g, &st);
    ASSERT_EQ(st, status_$proc2_pgroup_in_different_session);
}

TEST(set_session_id_arms)
{
    int8_t fl = 0; int16_t sid; status_$t st;
    E(2)->upid = 20; E(2)->session_id = 0; E(2)->pgroup_table_idx = 0;
    sid = 20; PROC2_$SET_SESSION_ID(&fl, &sid, &st);          /* own upid, no group */
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_cleanup, 1); ASSERT_EQ(E(2)->session_id, 20);
    ASSERT_EQ(n_set_internal, 1); ASSERT_EQ(last_set_upgid, 20);
    G(4)->ref_count = 1; G(4)->upgid = 20; E(2)->pgroup_table_idx = 4;
    PROC2_$SET_SESSION_ID(&fl, &sid, &st);                    /* I lead that group */
    ASSERT_EQ(st, status_$proc2_process_is_group_leader);
    fl = (int8_t)0x80; PROC2_$SET_SESSION_ID(&fl, &sid, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_cleanup, 2);
    E(2)->pgroup_table_idx = 5; fl = 0;
    PROC2_$SET_SESSION_ID(&fl, &sid, &st);                    /* someone else's group */
    ASSERT_EQ(st, status_$proc2_process_using_pgroup_id);
    sid = 33; E(2)->session_id = 9; E(2)->pgroup_table_idx = 5;
    PROC2_$SET_SESSION_ID(&fl, &sid, &st);
    ASSERT_EQ(st, status_$proc2_pgroup_in_different_session);
    E(2)->pgroup_table_idx = 0;
    PROC2_$SET_SESSION_ID(&fl, &sid, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(E(2)->session_id, 33);
    sid = 0; E(2)->pgroup_table_idx = 5; E(2)->session_id = 9;
    PROC2_$SET_SESSION_ID(&fl, &sid, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(E(2)->session_id, 0);
    ASSERT_EQ(n_lock, n_unlock);
}

TEST(set_server_high_byte)
{
    int8_t f = (int8_t)0x80; status_$t st;
    E(3)->flags = 0x0002;
    PROC2_$SET_SERVER(&U, &f, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(E(3)->flags, 0x0202);
    f = 0x7F; PROC2_$SET_SERVER(&U, &f, &st);
    ASSERT_EQ(E(3)->flags, 0x0002);
    mock_find_status = status_$proc2_uid_not_found; f = (int8_t)0xFF;
    PROC2_$SET_SERVER(&U, &f, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found); ASSERT_EQ(E(3)->flags, 0x0002);
}

TEST(set_priority_unsigned_order_and_low_word)
{
    uint16_t a = 0x8001, b = 3; status_$t st;
    E(3)->level1_pid = 17;
    PROC2_$SET_PRIORITY(&U, &a, &b, &st);
    ASSERT_EQ(n_p1_set, 1); ASSERT_EQ(last_p1_pid, 17); ASSERT_EQ(last_p1_mode, (int8_t)0xFF);
    ASSERT_EQ(last_p1_min, 3); ASSERT_EQ(last_p1_max, 0x8001);
    mock_find_status = 0x00010000;                     /* low word zero: proceeds */
    PROC2_$SET_PRIORITY(&U, &a, &b, &st);
    ASSERT_EQ(n_p1_set, 2); ASSERT_EQ(st, 0x00010000);
    mock_find_status = status_$proc2_uid_not_found;
    PROC2_$SET_PRIORITY(&U, &a, &b, &st);
    ASSERT_EQ(n_p1_set, 2);
}

TEST(set_acct_info_clamp_and_flag)
{
    uint8_t buf[40]; int16_t len = 40; uid_t au = { 0xA, 0xB }; status_$t st = 5;
    memset(buf, 'x', sizeof(buf));
    E(2)->flags = 0x0008 | 0x0100; memset(E(2)->acct_info, 0, 32);
    PROC2_$SET_ACCT_INFO(buf, &len, &au, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(E(2)->acct_info_len, 32);
    ASSERT_EQ(E(2)->acct_info[31], 'x'); ASSERT_EQ(E(2)->acct_uid.low, 0xB);
    ASSERT_EQ(E(2)->flags, 0x0100);
    len = -2; PROC2_$SET_ACCT_INFO(buf, &len, &au, &st);
    ASSERT_EQ(E(2)->acct_info_len, 0xFFFE);            /* stored unclamped */
}

TEST(set_cleanup_mask_and_as_gate)
{
    E(2)->cleanup_flags = 0;
    PROC2_$SET_CLEANUP(3); ASSERT_EQ(E(2)->cleanup_flags, 0x0008);
    PROC2_$SET_CLEANUP(35); ASSERT_EQ(E(2)->cleanup_flags, 0x0008);   /* 35 & 31 = 3 */
    PROC2_$SET_CLEANUP(20); ASSERT_EQ(E(2)->cleanup_flags, 0x0008);   /* bit 20: word drops it */
    PROC1_$AS_ID = 0; PROC2_$SET_CLEANUP(1); ASSERT_EQ(E(2)->cleanup_flags, 0x0008);
}

TEST(set_name_paths)
{
    int16_t len; status_$t st;
    len = 33; PROC2_$SET_NAME("x", &len, &U, &st);
    ASSERT_EQ(st, status_$proc2_invalid_process_name);
    len = 0; PROC2_$SET_NAME("x", &len, &U, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(E(3)->name_len, 0x22);
    len = 5; PROC2_$SET_NAME("hello", &len, &U, &st);
    ASSERT_EQ(E(3)->name_len, 5); ASSERT_EQ(E(3)->name[4], 'o');
    mock_find_status = status_$proc2_zombie; len = 1; PROC2_$SET_NAME("q", &len, &U, &st);
    ASSERT_EQ(st, status_$proc2_zombie); ASSERT_EQ(E(3)->name_len, 5);
    ASSERT_EQ(n_lock, n_unlock);
}

int main(void)
{
    RUN_TEST(set_pgroup_permission_chain);
    RUN_TEST(set_pgroup_group_validation);
    RUN_TEST(set_session_id_arms);
    RUN_TEST(set_server_high_byte);
    RUN_TEST(set_priority_unsigned_order_and_low_word);
    RUN_TEST(set_acct_info_clamp_and_flag);
    RUN_TEST(set_cleanup_mask_and_as_gate);
    RUN_TEST(set_name_paths);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
