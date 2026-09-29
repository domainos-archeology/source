/*
 * Tests for the PROC2 debug-attach family:
 *   DEBUG_UNLINK_FROM_LIST (0x00E418B0), DEBUG_SETUP_INTERNAL (0x00E4194C),
 *   DEBUG_CLEAR_INTERNAL (0x00E41A24) and PROC2_$DEBUG (0x00E41620).
 *
 * The real .c files are #included and driven over a mock process table.
 * Points pinned by the disassembly:
 *   - the fault-mode test is bit 4 of the LOW byte of the flags word
 *     (0x00E4199E `btst.b #0x4,(-0xb9,A2)`), i.e. flags & 0x0010;
 *   - XPD_$WRITE gets (asid, cr_rec_2 + 0x90, &1, &0xFFFFFFFF / &0, status)
 *     in that order (0x00E419E0.. / 0x00E41A78..);
 *   - DEBUG_CLEAR's zombie arm wakes the guardian and skips the write;
 *   - PROC2_$DEBUG with UID_$NIL attaches the caller to its own parent
 *     with flag 0, otherwise the caller becomes the debugger with flag 0xFF;
 *   - the unlink walk crashes when the target is not on the list.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"


MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);
MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);

uint16_t PROC1_$CURRENT;
uid_t UID_$NIL = { 0, 0 };

int __host_intr_disable_count = 0;

/* mock trace */
static int n_lock, n_unlock, n_awaken, n_reset, n_write, n_resume, n_crash;
static int16_t last_awaken_idx;
static uint16_t *last_write_asid;
static void *last_write_addr;
static int32_t last_write_len;
static uint32_t last_write_value;
static uint16_t last_resume_pid;
static int8_t mock_debug_rights;
static int16_t mock_find_index;
static status_$t mock_find_status;
static const status_$t *last_crash_status;
static jmp_buf crash_jmp;

static void reset_mocks(void)
{
    memset(PROC2_$DATA.info, 0, sizeof(PROC2_$DATA.info));
    memset(PROC2_$DATA.pid_to_index, 0, sizeof(PROC2_$DATA.pid_to_index));
    n_lock = n_unlock = n_awaken = n_reset = n_write = n_resume = n_crash = 0;
    last_awaken_idx = -1;
    last_write_asid = NULL;
    last_write_addr = NULL;
    last_write_len = 0;
    last_write_value = 0x12345678u;
    last_resume_pid = 0xFFFF;
    mock_debug_rights = (int8_t)0xFF;
    mock_find_index = 0;
    mock_find_status = status_$ok;
    last_crash_status = NULL;
    PROC1_$CURRENT = 5;
}

void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }

void PROC2_$AWAKEN_GUARDIAN(int16_t *proc_index)
{
    n_awaken++;
    last_awaken_idx = *proc_index;
}

void XPD_$RESET_PTRACE_OPTS(xpd_$ptrace_opts_t *opts)
{
    n_reset++;
    memset(opts, 0, sizeof(*opts));
    opts->flags = 0x5A;   /* visible marker to prove the copy-back */
}

void XPD_$WRITE(uint16_t *asid, void *addr, const int32_t *len,
                const void *buffer, status_$t *status_ret)
{
    n_write++;
    last_write_asid = asid;
    last_write_addr = addr;
    last_write_len = *len;
    last_write_value = *(const uint32_t *)buffer;
    *status_ret = status_$ok;
}

void PROC1_$RESUME(uint16_t pid, status_$t *status_ret)
{
    n_resume++;
    last_resume_pid = pid;
    *status_ret = status_$ok;
}

int8_t ACL_$CHECK_DEBUG_RIGHTS(int16_t *pid1, int16_t *pid2)
{
    (void)pid1; (void)pid2;
    return mock_debug_rights;
}

int16_t PROC2_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret)
{
    (void)proc_uid;
    *status_ret = mock_find_status;
    return mock_find_index;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    n_crash++;
    last_crash_status = status_p;
    longjmp(crash_jmp, 1);
}

#include "proc2/debug_unlink_from_list.c"
#include "proc2/debug_setup_internal.c"
#include "proc2/debug_clear_internal.c"
#include "proc2/debug.c"

/* ------------------------------------------------------------------ */

static int tests_run, tests_failed;
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { tests_run++; reset_mocks(); name(); } while (0)
#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("  FAIL %s:%d: %s == %lld, expected %s == %lld\n", \
               __FILE__, __LINE__, #a, _a, #b, _b); \
        tests_failed++; return; } } while (0)
#define ASSERT_PTR_EQ(a, b) do { \
    if ((const void *)(a) != (const void *)(b)) { \
        printf("  FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        tests_failed++; return; } } while (0)

static proc2_info_t *E(int idx) { return P2_INFO_ENTRY(idx); }

/* ---- DEBUG_UNLINK_FROM_LIST ---- */

TEST(unlink_not_debugged_is_noop)
{
    E(2)->first_debug_target_idx = 7;
    DEBUG_UNLINK_FROM_LIST(3);
    ASSERT_EQ(E(2)->first_debug_target_idx, 7);
    ASSERT_EQ(n_crash, 0);
}

TEST(unlink_head_of_list)
{
    /* debugger 2 -> targets 3 -> 4 */
    E(2)->first_debug_target_idx = 3;
    E(3)->debugger_idx = 2; E(3)->next_debug_target_idx = 4;
    E(4)->debugger_idx = 2; E(4)->next_debug_target_idx = 0;
    DEBUG_UNLINK_FROM_LIST(3);
    ASSERT_EQ(E(3)->debugger_idx, 0);
    ASSERT_EQ(E(2)->first_debug_target_idx, 4);
    ASSERT_EQ(E(4)->next_debug_target_idx, 0);
}

TEST(unlink_middle_of_list)
{
    /* debugger 2 -> targets 3 -> 4 -> 5 */
    E(2)->first_debug_target_idx = 3;
    E(3)->debugger_idx = 2; E(3)->next_debug_target_idx = 4;
    E(4)->debugger_idx = 2; E(4)->next_debug_target_idx = 5;
    E(5)->debugger_idx = 2; E(5)->next_debug_target_idx = 0;
    DEBUG_UNLINK_FROM_LIST(4);
    ASSERT_EQ(E(4)->debugger_idx, 0);
    ASSERT_EQ(E(2)->first_debug_target_idx, 3);
    ASSERT_EQ(E(3)->next_debug_target_idx, 5);
}

TEST(unlink_missing_crashes_with_uid_not_found)
{
    E(2)->first_debug_target_idx = 4;
    E(4)->debugger_idx = 2; E(4)->next_debug_target_idx = 0;
    E(3)->debugger_idx = 2;               /* claims 2, but 2 does not list 3 */
    if (setjmp(crash_jmp) == 0) {
        DEBUG_UNLINK_FROM_LIST(3);
        ASSERT_EQ(0, 1);                  /* must not get here */
    }
    ASSERT_EQ(n_crash, 1);
    ASSERT_EQ(*last_crash_status, status_$proc2_uid_not_found);
    ASSERT_EQ(E(3)->debugger_idx, 0);     /* cleared before the walk */
}

/* ---- DEBUG_SETUP_INTERNAL ---- */

TEST(setup_links_and_resets_flag_false)
{
    E(2)->first_debug_target_idx = 6;
    E(3)->cr_rec_2 = 0x1000;
    E(3)->asid = 9;
    E(3)->flags = 0x0000;
    memset(E(3)->ptrace_opts, 0xEE, 14);
    DEBUG_SETUP_INTERNAL(3, 2, 0);
    ASSERT_EQ(E(3)->debugger_idx, 2);
    ASSERT_EQ(E(3)->next_debug_target_idx, 6);
    ASSERT_EQ(E(2)->first_debug_target_idx, 3);
    ASSERT_EQ(n_reset, 1);
    ASSERT_EQ(E(3)->ptrace_opts[0], 0);      /* copied back from the local */
    ASSERT_EQ(E(3)->ptrace_opts[12], 0x5A);
    ASSERT_EQ(n_write, 0);                   /* flag not negative */
    ASSERT_EQ(n_awaken, 0);                  /* flags & 0x0010 clear */
}

TEST(setup_flag_true_writes_ffffffff_and_wakes_twice)
{
    E(3)->cr_rec_2 = 0x1000;
    E(3)->asid = 9;
    E(3)->flags = 0x0010;                    /* fault mode: LOW byte bit 4 */
    DEBUG_SETUP_INTERNAL(3, 2, (int8_t)0xFF);
    ASSERT_EQ(n_awaken, 2);
    ASSERT_EQ(last_awaken_idx, 3);
    ASSERT_EQ(n_write, 1);
    ASSERT_PTR_EQ(last_write_asid, &E(3)->asid);
    ASSERT_PTR_EQ(last_write_addr, ARCH_VA_TO_PTR(0x1090));
    ASSERT_EQ(last_write_len, 1);
    ASSERT_EQ(last_write_value, 0xFFFFFFFFu);
}

TEST(setup_high_byte_bit4_is_not_fault_mode)
{
    E(3)->flags = 0x1000;                    /* bit 12, not bit 4 */
    DEBUG_SETUP_INTERNAL(3, 2, 0);
    ASSERT_EQ(n_awaken, 0);
}

TEST(setup_unlinks_previous_debugger_first)
{
    /* 3 is currently debugged by 4 */
    E(4)->first_debug_target_idx = 3;
    E(3)->debugger_idx = 4; E(3)->next_debug_target_idx = 0;
    DEBUG_SETUP_INTERNAL(3, 2, 0);
    ASSERT_EQ(E(4)->first_debug_target_idx, 0);
    ASSERT_EQ(E(3)->debugger_idx, 2);
    ASSERT_EQ(E(2)->first_debug_target_idx, 3);
}

/* ---- DEBUG_CLEAR_INTERNAL ---- */

TEST(clear_not_debugged_is_noop)
{
    E(3)->flags = 0x0010;
    DEBUG_CLEAR_INTERNAL(3, (int8_t)0xFF);
    ASSERT_EQ(E(3)->flags, 0x0010);
    ASSERT_EQ(n_write, 0);
}

TEST(clear_zombie_wakes_guardian_only)
{
    E(2)->first_debug_target_idx = 3;
    E(3)->debugger_idx = 2;
    E(3)->flags = PROC2_FLAG_ZOMBIE | 0x0010;
    DEBUG_CLEAR_INTERNAL(3, (int8_t)0xFF);
    ASSERT_EQ(E(3)->debugger_idx, 0);
    ASSERT_EQ(E(3)->flags, PROC2_FLAG_ZOMBIE);
    ASSERT_EQ(n_awaken, 1);
    ASSERT_EQ(last_awaken_idx, 3);
    ASSERT_EQ(n_write, 0);
    ASSERT_EQ(n_resume, 0);
}

TEST(clear_live_flag_true_writes_zero_and_resumes)
{
    E(2)->first_debug_target_idx = 3;
    E(3)->debugger_idx = 2;
    E(3)->flags = 0x0010;
    E(3)->cr_rec_2 = 0x2000;
    E(3)->asid = 4;
    E(3)->level1_pid = 17;
    DEBUG_CLEAR_INTERNAL(3, (int8_t)0xFF);
    ASSERT_EQ(n_write, 1);
    ASSERT_PTR_EQ(last_write_asid, &E(3)->asid);
    ASSERT_PTR_EQ(last_write_addr, ARCH_VA_TO_PTR(0x2090));
    ASSERT_EQ(last_write_len, 1);
    ASSERT_EQ(last_write_value, 0u);
    ASSERT_EQ(n_resume, 1);
    ASSERT_EQ(last_resume_pid, 17);
    ASSERT_EQ(E(3)->flags, 0);
}

TEST(clear_live_flag_false_skips_write)
{
    E(2)->first_debug_target_idx = 3;
    E(3)->debugger_idx = 2;
    E(3)->flags = 0x0010;
    DEBUG_CLEAR_INTERNAL(3, 0);
    ASSERT_EQ(n_write, 0);
    ASSERT_EQ(n_resume, 0);
    ASSERT_EQ(E(3)->flags, 0);
}

/* ---- PROC2_$DEBUG ---- */

TEST(debug_nil_uid_attaches_caller_to_parent)
{
    uid_t nil = { 0, 0 };
    status_$t st = 0x5555;
    PROC2_$DATA.pid_to_index[5] = 3;             /* caller is entry 3 */
    E(3)->parent_pgroup_idx = 2;
    PROC2_$DEBUG(&nil, &st);
    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(E(3)->debugger_idx, 2);
    ASSERT_EQ(E(2)->first_debug_target_idx, 3);
    ASSERT_EQ(n_write, 0);                /* flag 0 */
    ASSERT_EQ(n_lock, 1);
    ASSERT_EQ(n_unlock, 1);
}

TEST(debug_find_index_failure_propagates)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    mock_find_status = status_$proc2_uid_not_found;
    PROC2_$DEBUG(&u, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found);
    ASSERT_EQ(n_unlock, 1);
}

TEST(debug_already_debugged)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    mock_find_index = 4;
    E(4)->debugger_idx = 6;
    PROC2_$DEBUG(&u, &st);
    ASSERT_EQ(st, status_$proc2_process_already_debugging);
}

TEST(debug_permission_denied_on_non_negative_rights)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    mock_find_index = 4;
    mock_debug_rights = 0;
    PROC2_$DEBUG(&u, &st);
    ASSERT_EQ(st, status_$proc2_permission_denied);
    ASSERT_EQ(E(4)->debugger_idx, 0);
}

TEST(debug_success_caller_becomes_debugger_with_flag_true)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    mock_find_index = 4;
    PROC2_$DATA.pid_to_index[5] = 3;
    E(4)->cr_rec_2 = 0x3000;
    PROC2_$DEBUG(&u, &st);
    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(E(4)->debugger_idx, 3);
    ASSERT_EQ(E(3)->first_debug_target_idx, 4);
    ASSERT_EQ(n_write, 1);                /* flag 0xFF */
    ASSERT_EQ(last_write_value, 0xFFFFFFFFu);
}

int main(void)
{
    RUN_TEST(unlink_not_debugged_is_noop);
    RUN_TEST(unlink_head_of_list);
    RUN_TEST(unlink_middle_of_list);
    RUN_TEST(unlink_missing_crashes_with_uid_not_found);
    RUN_TEST(setup_links_and_resets_flag_false);
    RUN_TEST(setup_flag_true_writes_ffffffff_and_wakes_twice);
    RUN_TEST(setup_high_byte_bit4_is_not_fault_mode);
    RUN_TEST(setup_unlinks_previous_debugger_first);
    RUN_TEST(clear_not_debugged_is_noop);
    RUN_TEST(clear_zombie_wakes_guardian_only);
    RUN_TEST(clear_live_flag_true_writes_zero_and_resumes);
    RUN_TEST(clear_live_flag_false_skips_write);
    RUN_TEST(debug_nil_uid_attaches_caller_to_parent);
    RUN_TEST(debug_find_index_failure_propagates);
    RUN_TEST(debug_already_debugged);
    RUN_TEST(debug_permission_denied_on_non_negative_rights);
    RUN_TEST(debug_success_caller_becomes_debugger_with_flag_true);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
