/*
 * Tests for PGROUP_CLEANUP_INTERNAL (0x00E420B8) and
 * PROC2_$LOG_SIGNAL_EVENT (0x00E3E748).
 *
 * Pinned by the disassembly: mode 1 skips the leader-count pass, mode 0
 * skips the ref-count drop; the parent test drops a leader from THIS
 * entry's group, the child test from the CHILD's group, both only across
 * groups within one session.  LOG_SIGNAL_EVENT builds the 0x12-byte
 * record with uid.low = (type << 24) + 0xFDED and passes length 10.
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
int8_t AUDIT_$ENABLED;
int __host_intr_disable_count = 0;

static int n_decr; static int16_t decr_idx[4];
void PGROUP_DECR_LEADER_COUNT(int16_t idx) { if (n_decr < 4) decr_idx[n_decr] = idx; n_decr++; }

static int n_log; static uid_t log_uid; static uint16_t log_flags; static int32_t log_success;
static uint8_t log_data[10]; static uint16_t log_len;
void AUDIT_$LOG_EVENT(uid_t *u, uint16_t *f, status_$t *s, char *d, const uint16_t *l)
{ n_log++; log_uid = *u; log_flags = *f; log_success = *s; memcpy(log_data, d, 10); log_len = *l; }

#include "proc2/pgroup_cleanup_internal.c"
#include "proc2/log_signal_event.c"

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
    n_decr = 0; n_log = 0; AUDIT_$ENABLED = 0;
    /* entry 3 in group 4 session 9, parent 2 in group 5 session 9,
     * children 6 (group 7, session 9) and 7 (group 4) */
    E(3)->pgroup_table_idx = 4; E(3)->session_id = 9; E(3)->parent_pgroup_idx = 2;
    E(3)->first_child_idx = 6; E(6)->next_child_sibling = 7;
    E(2)->pgroup_table_idx = 5; E(2)->session_id = 9;
    E(6)->pgroup_table_idx = 7; E(6)->session_id = 9;
    E(7)->pgroup_table_idx = 4; E(7)->session_id = 9;
    PROC2_$DATA.pgroup[4].ref_count = 3;
}

TEST(mode_2_full)
{
    PGROUP_CLEANUP_INTERNAL(E(3), 2);
    ASSERT_EQ(n_decr, 2);
    ASSERT_EQ(decr_idx[0], 4);      /* parent differs -> this entry's group */
    ASSERT_EQ(decr_idx[1], 7);      /* child 6's group; child 7 same group */
    ASSERT_EQ(PROC2_$DATA.pgroup[4].ref_count, 2);
    ASSERT_EQ(E(3)->pgroup_table_idx, 0);
}

TEST(mode_1_refcount_only)
{
    PGROUP_CLEANUP_INTERNAL(E(3), 1);
    ASSERT_EQ(n_decr, 0);
    ASSERT_EQ(PROC2_$DATA.pgroup[4].ref_count, 2);
    ASSERT_EQ(E(3)->pgroup_table_idx, 0);
}

TEST(mode_0_leaders_only)
{
    PGROUP_CLEANUP_INTERNAL(E(3), 0);
    ASSERT_EQ(n_decr, 2);
    ASSERT_EQ(PROC2_$DATA.pgroup[4].ref_count, 3);
    ASSERT_EQ(E(3)->pgroup_table_idx, 4);
}

TEST(other_session_and_no_parent_and_no_group)
{
    E(2)->session_id = 8; E(6)->session_id = 8;
    PGROUP_CLEANUP_INTERNAL(E(3), 2);
    ASSERT_EQ(n_decr, 0);
    E(3)->pgroup_table_idx = 0;
    PGROUP_CLEANUP_INTERNAL(E(3), 2);
    ASSERT_EQ(PROC2_$DATA.pgroup[4].ref_count, 2);   /* second call was a no-op */
    E(5)->pgroup_table_idx = 4; E(5)->session_id = 9; E(5)->parent_pgroup_idx = 0;
    PGROUP_CLEANUP_INTERNAL(E(5), 2);
    ASSERT_EQ(n_decr, 0);
}

TEST(log_disabled_and_process_event)
{
    PROC2_$LOG_SIGNAL_EVENT(1, 3, 9, 0x11223344u, 0);
    ASSERT_EQ(n_log, 0);
    AUDIT_$ENABLED = (int8_t)0xFF;
    E(3)->asid = 0x21; E(3)->upid = 0x4321;
    PROC2_$LOG_SIGNAL_EVENT(1, 3, 9, 0x11223344u, 5);
    ASSERT_EQ(n_log, 1);
    ASSERT_EQ(log_uid.high, 0x4165836Cu); ASSERT_EQ(log_uid.low, 0x0100FDEDu);
    ASSERT_EQ(log_flags, 1); ASSERT_EQ(log_success, 5); ASSERT_EQ(log_len, 10);
    {
        signal_audit_event_t *e = (signal_audit_event_t *)(log_data - 8);
        (void)e;
        uint32_t param; uint16_t asid, sig, upid;
        memcpy(&param, log_data, 4); memcpy(&asid, log_data + 4, 2);
        memcpy(&sig, log_data + 6, 2); memcpy(&upid, log_data + 8, 2);
        ASSERT_EQ(param, 0x11223344u); ASSERT_EQ(asid, 0x21); ASSERT_EQ(sig, 9); ASSERT_EQ(upid, 0x4321);
    }
}

TEST(log_pgroup_event)
{
    AUDIT_$ENABLED = (int8_t)0xFF;
    PROC2_$DATA.pgroup[4].upgid = 0x777;
    PROC2_$LOG_SIGNAL_EVENT(2, 4, 15, 0, 0);
    uint16_t asid, upid; memcpy(&asid, log_data + 4, 2); memcpy(&upid, log_data + 8, 2);
    ASSERT_EQ(log_uid.low, 0x0200FDEDu); ASSERT_EQ(log_flags, 0);
    ASSERT_EQ(asid, 0); ASSERT_EQ(upid, 0x777);
}

int main(void)
{
    RUN_TEST(mode_2_full);
    RUN_TEST(mode_1_refcount_only);
    RUN_TEST(mode_0_leaders_only);
    RUN_TEST(other_session_and_no_parent_and_no_group);
    RUN_TEST(log_disabled_and_process_event);
    RUN_TEST(log_pgroup_event);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
