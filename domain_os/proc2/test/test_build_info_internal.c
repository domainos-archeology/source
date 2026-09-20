/*
 * Tests for PROC2_$BUILD_INFO_INTERNAL (0x00E4094C).
 *
 * Pinned by the disassembly:
 *   - proc1_pid == 0 clears the PROC1 half (0x00E40A0E..) and leaves
 *     out+0xD6 alone;
 *   - a PROC1_$GET_INFO / ACL_$GET_PID_SID failure sets bit 31 and exits
 *     before the PROC2 half (0x00E409B6);
 *   - the "current process" status 0x00190004 (0x00E40A06);
 *   - neither bound nor zombie -> 0x00190002 and NO common tail;
 *   - the zombie arm copies entry+0xA4.. into proc1_info.cpu_total and
 *     out+0xD0.. (0x00E40B7E..0x00E40B96) and sets 0x0019000E;
 *   - name_len '!' -> 0, '"' -> 0xFFFF;
 *   - server_flag = flags bit 9 (sne), parent_upid defaults to 1.
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
uint16_t *PROC2_$PID_TO_INDEX = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
uint16_t PROC1_$CURRENT;
uid_t UID_$NIL = { 0, 0 };
uint32_t PROC_STATS_BASE[PROC1_MAX_PROCESSES * 4];
int __host_intr_disable_count = 0;

static status_$t mock_get_info_status, mock_sid_status;
static int n_get_info, n_set_priority, n_sid, n_usage;

static void reset_mocks(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(mock_pgroups, 0, sizeof(mock_pgroups));
    memset(PROC_STATS_BASE, 0, sizeof(PROC_STATS_BASE));
    mock_get_info_status = mock_sid_status = status_$ok;
    n_get_info = n_set_priority = n_sid = n_usage = 0;
    PROC1_$CURRENT = 5;
}

void PROC1_$GET_INFO(int16_t *pidp, proc1_$info_t *info, status_$t *s)
{
    n_get_info++;
    memset(info, 0x11, sizeof(*info));
    info->upc = (uint32_t)*pidp;
    *s = mock_get_info_status;
}
void PROC1_$SET_PRIORITY(uint16_t pid, int8_t mode, uint16_t *min_p, uint16_t *max_p)
{
    (void)pid; (void)mode; n_set_priority++; *min_p = 0x0301; *max_p = 0x0E01;
}
void ACL_$GET_PID_SID(int16_t pid, uid_t *sid, status_$t *s)
{
    n_sid++;
    for (int i = 0; i < 4; i++) { sid[i].high = 0x51D00000u + i; sid[i].low = (uint32_t)pid; }
    *s = mock_sid_status;
}
void PROC1_$GET_ANY_CPU_USAGE(uint16_t *pid_ptr, void *cpu, uint32_t *s1, uint32_t *s2)
{
    n_usage++;
    *(uint32_t *)cpu = 0xD0D0D0D0u | *pid_ptr;
    *s1 = 0xDCDCDCDCu; *s2 = 0xD8D8D8D8u;
}

#include "proc2/build_info_internal.c"

static int tests_run, tests_failed;
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { tests_run++; reset_mocks(); name(); } while (0)
#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("  FAIL %s:%d: %s == %lld, expected %s == %lld\n", \
               __FILE__, __LINE__, #a, _a, #b, _b); \
        tests_failed++; return; } } while (0)

static proc_info_combined_t out;
static proc2_info_t *E(int i) { return P2_INFO_ENTRY(i); }

TEST(no_proc1_half_clears_and_keeps_d6)
{
    status_$t st = 0x77;
    memset(&out, 0xEE, sizeof(out));
    PROC2_$BUILD_INFO_INTERNAL(0, 0, &out, &st);
    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(n_get_info, 0);
    ASSERT_EQ(out.proc1_info.upc, 0);
    ASSERT_EQ(out.min_priority, 0); ASSERT_EQ(out.max_priority, 0);
    ASSERT_EQ(out.sid[3].high, 0); ASSERT_EQ(out.pad_44, 0);
    ASSERT_EQ(out.cpu_time[3], 0);
    ASSERT_EQ(out.usage_d0, 0); ASSERT_EQ(out.usage_d4, 0);
    ASSERT_EQ(out.pad_d6, 0xEEEE);           /* untouched */
    ASSERT_EQ(out.usage_d8, 0); ASSERT_EQ(out.usage_dc, 0); ASSERT_EQ(out.const_e0, 0);
    ASSERT_EQ(out.parent_uid.high, 0xEEEEEEEEu);  /* PROC2 half skipped */
}

TEST(proc1_half_fills_and_flags_current)
{
    status_$t st = 0;
    memset(&out, 0, sizeof(out));
    PROC_STATS_BASE[5 * 4 + 0] = 1; PROC_STATS_BASE[5 * 4 + 3] = 4;
    PROC2_$BUILD_INFO_INTERNAL(0, 5, &out, &st);
    ASSERT_EQ(st, status_$proc2_request_is_for_current_process);
    ASSERT_EQ(out.proc1_info.upc, 5);
    ASSERT_EQ(out.min_priority, 0x0301); ASSERT_EQ(out.max_priority, 0x0E01);
    ASSERT_EQ(out.sid[2].high, 0x51D00002u);
    ASSERT_EQ(out.cpu_time[0], 1); ASSERT_EQ(out.cpu_time[3], 4);
    ASSERT_EQ(out.usage_d0, 0xD0D0D0D5u);
    ASSERT_EQ(out.usage_dc, 0xDCDCDCDCu); ASSERT_EQ(out.usage_d8, 0xD8D8D8D8u);
    ASSERT_EQ(out.const_e0, 0x411C);
}

TEST(get_info_failure_sets_bit31_and_exits)
{
    status_$t st = 0;
    memset(&out, 0xEE, sizeof(out));
    mock_get_info_status = 0x000C0001;
    PROC2_$BUILD_INFO_INTERNAL(3, 6, &out, &st);
    ASSERT_EQ((uint32_t)st, 0x800C0001u);
    ASSERT_EQ(n_set_priority, 0);
    ASSERT_EQ(out.parent_uid.high, 0xEEEEEEEEu);
}

TEST(sid_failure_sets_bit31)
{
    status_$t st = 0;
    mock_sid_status = 0x00070002;
    PROC2_$BUILD_INFO_INTERNAL(3, 6, &out, &st);
    ASSERT_EQ((uint32_t)st, 0x80070002u);
    ASSERT_EQ(n_usage, 0);
}

TEST(not_level2_process_clears_no_common_tail)
{
    status_$t st = 0;
    memset(&out, 0xEE, sizeof(out));
    E(3)->flags = 0x0200; E(3)->upid = 42;
    PROC2_$BUILD_INFO_INTERNAL(3, 0, &out, &st);
    ASSERT_EQ(st, status_$proc2_not_level_2_process);
    ASSERT_EQ(out.upid, 0);
    ASSERT_EQ(out.server_flag, 0);            /* not from the tail */
    ASSERT_EQ(out.parent_upid, 0); ASSERT_EQ(out.pgroup_info, 0);
    ASSERT_EQ(out.acct_info[0], (char)0xEE);  /* tail copy not run */
}

TEST(bound_process_fields)
{
    status_$t st = 0;
    memset(&out, 0, sizeof(out));
    E(3)->flags = 0x0100 | 0x0200;
    E(3)->parent_uid.high = 0xAA; E(3)->cr_rec = 0xC0DE; E(3)->asid = 7;
    E(3)->pgroup_table_idx = 2; mock_pgroups[2].upgid = 0x1234;
    E(3)->tty_uid.low = 0x77;
    E(3)->name_len = 5; memset(E(3)->name, 'n', 32);
    E(3)->upid = 100; E(3)->parent_pgroup_idx = 4; E(4)->upid = 90;
    E(3)->debugger_idx = 5; E(5)->upid = 80;
    E(3)->acct_uid.high = 0xACC7; E(3)->acct_info_len = 3; memset(E(3)->acct_info, 'a', 32);
    PROC2_$BUILD_INFO_INTERNAL(3, 0, &out, &st);
    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(out.parent_uid.high, 0xAA); ASSERT_EQ(out.cr_rec, 0xC0DE); ASSERT_EQ(out.asid, 7);
    ASSERT_EQ(out.proc_uid_2.high, 0x1234); ASSERT_EQ(out.proc_uid_2.low, 0);
    ASSERT_EQ(out.pgroup_uid.high, 0x1234);
    ASSERT_EQ(out.pgroup_info, 0x1234); ASSERT_EQ(out.pgroup_flags, 0x1234);
    ASSERT_EQ(out.tty_uid.low, 0x77);
    ASSERT_EQ(out.name_len, 5); ASSERT_EQ(out.name[31], 'n');
    ASSERT_EQ(out.server_flag, (int8_t)0xFF);
    ASSERT_EQ(out.upid, 100); ASSERT_EQ(out.parent_upid, 90); ASSERT_EQ(out.session_upid, 80);
    ASSERT_EQ(out.acct_uid.high, 0xACC7); ASSERT_EQ(out.acct_info_len, 3);
    ASSERT_EQ(out.acct_info[31], 'a');
}

TEST(name_len_sentinels_and_default_parent)
{
    status_$t st = 0;
    E(3)->flags = 0x0100; E(3)->name_len = 0x21;
    PROC2_$BUILD_INFO_INTERNAL(3, 0, &out, &st);
    ASSERT_EQ(out.name_len, 0);
    ASSERT_EQ(out.parent_upid, 1);
    ASSERT_EQ(out.session_upid, 0);
    ASSERT_EQ(out.server_flag, 0);
    E(3)->name_len = 0x22;
    PROC2_$BUILD_INFO_INTERNAL(3, 0, &out, &st);
    ASSERT_EQ(out.name_len, 0xFFFF);
}

TEST(zombie_arm)
{
    status_$t st = 0;
    memset(&out, 0xEE, sizeof(out));
    E(3)->flags = PROC2_FLAG_ZOMBIE;
    for (int i = 0; i < 20; i++) E(3)->zombie_usage[i] = (uint8_t)(0xA4 + i);
    E(3)->upid = 7;
    /* proc1_pid 6 (not current): PROC1_$GET_INFO fills 0x11 first */
    PROC2_$BUILD_INFO_INTERNAL(3, 6, &out, &st);
    ASSERT_EQ(st, status_$proc2_zombie);
    ASSERT_EQ(out.parent_uid.high, 0); ASSERT_EQ(out.cr_rec, 0); ASSERT_EQ(out.asid, 0);
    ASSERT_EQ(out.tty_uid.high, 0);
    ASSERT_EQ(out.proc1_info.cpu_total[0], 0xA4);
    ASSERT_EQ(out.proc1_info.cpu_total[5], 0xA9);
    ASSERT_EQ(out.proc1_info.cpu_total[6], 0x11);   /* only six bytes */
    ASSERT_EQ(out.usage_d0, 0xA4A5A6A7u);
    ASSERT_EQ(out.usage_d4, 0xA8A9); ASSERT_EQ(out.pad_d6, 0xAAAB);
    ASSERT_EQ(out.usage_d8, 0xACADAEAFu);
    ASSERT_EQ(out.const_e0, 0xB4B5B6B7u);
    ASSERT_EQ(out.name_len, 0);
    ASSERT_EQ(out.upid, 7);                        /* common tail ran */
    ASSERT_EQ(out.parent_upid, 1);
}

int main(void)
{
    RUN_TEST(no_proc1_half_clears_and_keeps_d6);
    RUN_TEST(proc1_half_fills_and_flags_current);
    RUN_TEST(get_info_failure_sets_bit31_and_exits);
    RUN_TEST(sid_failure_sets_bit31);
    RUN_TEST(not_level2_process_clears_no_common_tail);
    RUN_TEST(bound_process_fields);
    RUN_TEST(name_len_sentinels_and_default_parent);
    RUN_TEST(zombie_arm);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
