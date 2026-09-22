/*
 * Tests for the signal entry points: PROC2_$SIGNAL (0x00E3EFA0),
 * PROC2_$SIGNAL_PGROUP_INTERNAL (0x00E3F160), PROC2_$SIGNAL_PGROUP
 * (0x00E3F23E), PROC2_$SIGNAL_PGROUP_OS (0x00E3F2C2), PROC2_$SIGBLOCK
 * (0x00E3F63E), PROC2_$SHUTDOWN (0x00E415C2), PROC2_$SET_TTY (0x00E41C04).
 *
 * Pinned by the disassembly: SIGNAL's three-way permission chain (debugger
 * == caller, same non-zero session with signal 0x16, ACL) and zombie
 * handling; PGROUP_INTERNAL's continue-on-denial, self-session quirk and
 * final-status ladder plus the unconditional audit call; the TRUE/FALSE
 * check_perms of the two wrappers; SIGBLOCK's return value; SHUTDOWN's
 * ASID and BOUND filters.
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
uint16_t P2_INFO_ALLOC_PTR;
uint16_t *PROC2_$PID_TO_INDEX = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
uint16_t PROC1_$CURRENT, PROC1_$AS_ID;
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_deliver, n_log, n_suspend, n_acl;
static int16_t mock_find_index, mock_pgroup_idx; static status_$t mock_find_status;
static int8_t mock_acl; static status_$t mock_deliver_status;
static int16_t deliver_idx[4], deliver_sig[4], log_type, log_idx; static status_$t log_status;
static const uint16_t *last_acl_1, *last_acl_2;
static uid_t *suspend_uid[4];

void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
int16_t PROC2_$FIND_INDEX(uid_t *u, status_$t *s) { (void)u; *s = mock_find_status; return mock_find_index; }
int16_t PROC2_$UID_TO_PGROUP_INDEX(uid_t *u) { (void)u; return mock_pgroup_idx; }
int8_t ACL_$CHECK_FAULT_RIGHTS(const uint16_t *a, const uint16_t *b) { n_acl++; last_acl_1 = a; last_acl_2 = b; return mock_acl; }
void PROC2_$DELIVER_SIGNAL_INTERNAL(int16_t i, int16_t sig, int32_t p, status_$t *s)
{ (void)p; if (n_deliver < 4) { deliver_idx[n_deliver] = i; deliver_sig[n_deliver] = sig; } n_deliver++; *s = mock_deliver_status; }
void PROC2_$LOG_SIGNAL_EVENT(uint16_t t, int16_t i, uint16_t sig, uint32_t p, int32_t st)
{ (void)sig; (void)p; n_log++; log_type = (int16_t)t; log_idx = i; log_status = st; }
void PROC2_$SUSPEND(uid_t *u, status_$t *s) { if (n_suspend < 4) suspend_uid[n_suspend] = u; n_suspend++; *s = status_$ok; }

#include "proc2/signal.c"
#include "proc2/signal_pgroup_internal.c"
#include "proc2/signal_pgroup.c"
#include "proc2/signal_pgroup_os.c"
#include "proc2/sigblock.c"
#include "proc2/shutdown.c"
#include "proc2/set_tty.c"

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
static uid_t U = { 1, 2 };
static void reset(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    n_lock = n_unlock = n_deliver = n_log = n_suspend = n_acl = 0;
    mock_find_index = 3; mock_find_status = status_$ok; mock_pgroup_idx = 4;
    mock_acl = 0; mock_deliver_status = status_$ok;
    PROC1_$CURRENT = 5; PROC1_$AS_ID = 7; mock_pid_to_index[5] = 2;
    E(2)->self_index = 2; E(2)->session_id = 9; E(2)->level1_pid = 5;
    E(3)->session_id = 9; E(3)->level1_pid = 17;
    P2_INFO_ALLOC_PTR = 0;
}

TEST(signal_permission_chain)
{
    int16_t sig = 9; uint32_t p = 0x55; status_$t st;
    PROC2_$SIGNAL(&U, &sig, &p, &st);                        /* ACL denies */
    ASSERT_EQ(st, status_$proc2_permission_denied); ASSERT_EQ(n_deliver, 0);
    ASSERT_EQ(n_acl, 1); ASSERT_EQ(last_acl_1 == &E(2)->level1_pid, 1); ASSERT_EQ(last_acl_2 == &E(3)->level1_pid, 1);
    ASSERT_EQ(n_log, 1); ASSERT_EQ(log_type, 1); ASSERT_EQ(log_idx, 3); ASSERT_EQ(log_status, status_$proc2_permission_denied);
    E(3)->debugger_idx = 2;                                  /* I debug it */
    PROC2_$SIGNAL(&U, &sig, &p, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_deliver, 1); ASSERT_EQ(deliver_idx[0], 3); ASSERT_EQ(n_acl, 1);
    E(3)->debugger_idx = 0; sig = 0x16;                      /* same session + 0x16 */
    PROC2_$SIGNAL(&U, &sig, &p, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_deliver, 2); ASSERT_EQ(n_acl, 1);
    E(3)->session_id = 0; E(2)->session_id = 0;              /* session 0: no bypass */
    PROC2_$SIGNAL(&U, &sig, &p, &st);
    ASSERT_EQ(st, status_$proc2_permission_denied); ASSERT_EQ(n_acl, 2);
    mock_acl = (int8_t)0xFF; mock_find_status = status_$proc2_zombie;
    PROC2_$SIGNAL(&U, &sig, &p, &st);                        /* zombie: allowed, not delivered */
    ASSERT_EQ(st, status_$proc2_zombie); ASSERT_EQ(n_deliver, 2); ASSERT_EQ(n_acl, 3);
    mock_find_status = status_$proc2_uid_not_found;
    PROC2_$SIGNAL(&U, &sig, &p, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found); ASSERT_EQ(n_acl, 3);
    ASSERT_EQ(n_lock, n_unlock); ASSERT_EQ(n_log, 6);
}

TEST(pgroup_internal_ladder)
{
    status_$t st = 5;
    PROC2_$SIGNAL_PGROUP_INTERNAL(0, 1, 0, 0, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found); ASSERT_EQ(n_log, 1); ASSERT_EQ(log_type, 2);
    P2_INFO_ALLOC_PTR = 3; E(3)->next_index = 4; E(4)->next_index = 6; E(6)->next_index = 0;
    E(3)->pgroup_table_idx = 4; E(4)->pgroup_table_idx = 4; E(6)->pgroup_table_idx = 5;
    PROC2_$SIGNAL_PGROUP_INTERNAL(4, 1, 0, 0, &st);          /* no perms: both */
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_deliver, 2); ASSERT_EQ(deliver_idx[1], 4);
    E(3)->flags = PROC2_FLAG_ZOMBIE; n_deliver = 0;
    PROC2_$SIGNAL_PGROUP_INTERNAL(4, 1, 0, (int8_t)0xFF, &st);   /* ACL denies 4 */
    /* nobody delivered to and a zombie seen: the ladder overrides the
     * permission_denied stored in the loop (0x00E3F20A..0x00E3F214) */
    ASSERT_EQ(st, status_$proc2_zombie); ASSERT_EQ(n_deliver, 0);
    PROC2_$SIGNAL_PGROUP_INTERNAL(4, 0x16, 0, (int8_t)0xFF, &st);  /* 0x16: self-session quirk passes */
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_deliver, 1); ASSERT_EQ(deliver_idx[0], 4);
    E(4)->flags = PROC2_FLAG_ZOMBIE; n_deliver = 0;
    PROC2_$SIGNAL_PGROUP_INTERNAL(4, 1, 0, 0, &st);          /* only zombies */
    ASSERT_EQ(st, status_$proc2_zombie); ASSERT_EQ(n_deliver, 0);
    PROC2_$SIGNAL_PGROUP_INTERNAL(7, 1, 0, 0, &st);          /* no members */
    ASSERT_EQ(st, status_$proc2_uid_not_found);
    ASSERT_EQ(n_log, 6); ASSERT_EQ(log_status, status_$proc2_uid_not_found);
}

TEST(pgroup_wrappers_and_check_perms)
{
    int16_t sig = 2; uint32_t p = 0; status_$t st;
    P2_INFO_ALLOC_PTR = 3; E(3)->pgroup_table_idx = 4;
    PROC2_$SIGNAL_PGROUP(&U, &sig, &p, &st);                 /* perms on: ACL denies */
    /* denied, nobody delivered to, no zombie -> uid_not_found (0x00E3F21C) */
    ASSERT_EQ(st, status_$proc2_uid_not_found); ASSERT_EQ(n_acl, 1);
    PROC2_$SIGNAL_PGROUP_OS(&U, &sig, &p, &st);              /* perms off */
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_acl, 1); ASSERT_EQ(n_deliver, 1);
    ASSERT_EQ(n_lock, 2); ASSERT_EQ(n_unlock, 2);
}

TEST(sigblock_returns_old_mask)
{
    uint32_t m = 0x0F, r[2] = { 0, 0 };
    E(2)->sig_blocked_2 = 0xF0; E(2)->flags = 0x0400;
    ASSERT_EQ(PROC2_$SIGBLOCK(&m, r), 0xF0);
    ASSERT_EQ(E(2)->sig_blocked_2, 0xFF); ASSERT_EQ(r[0], 0xFF); ASSERT_EQ(r[1], 1);
    E(2)->flags = 0;
    PROC2_$SIGBLOCK(&m, r); ASSERT_EQ(r[1], 0);
    ASSERT_EQ(n_lock, n_unlock);
}

TEST(shutdown_filters)
{
    P2_INFO_ALLOC_PTR = 2; E(2)->next_index = 3; E(3)->next_index = 4; E(4)->next_index = 0;
    E(2)->asid = 7; E(2)->flags = 0x0100;      /* my own AS */
    E(3)->asid = 8; E(3)->flags = 0x0100;      /* suspended */
    E(4)->asid = 9; E(4)->flags = 0x8000;      /* not bound */
    PROC2_$SHUTDOWN();
    ASSERT_EQ(n_suspend, 1); ASSERT_EQ(suspend_uid[0] == &E(3)->uid, 1);
    ASSERT_EQ(n_lock, 0);
    P2_INFO_ALLOC_PTR = 0; PROC2_$SHUTDOWN(); ASSERT_EQ(n_suspend, 1);
}

TEST(set_tty)
{
    uid_t t = { 0xAA, 0xBB };
    PROC2_$SET_TTY(&t);
    ASSERT_EQ(E(2)->tty_uid.high, 0xAA); ASSERT_EQ(E(2)->tty_uid.low, 0xBB); ASSERT_EQ(n_lock, 0);
}

int main(void)
{
    RUN_TEST(signal_permission_chain);
    RUN_TEST(pgroup_internal_ladder);
    RUN_TEST(pgroup_wrappers_and_check_perms);
    RUN_TEST(sigblock_returns_old_mask);
    RUN_TEST(shutdown_filters);
    RUN_TEST(set_tty);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
