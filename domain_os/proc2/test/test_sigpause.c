/*
 * Tests for PROC2_$SIGPAUSE (0x00E3FA10), PROC2_$SIGRETURN (0x00E3F582),
 * PROC2_$SIGSETMASK (0x00E3F6C0), PROC2_$UID_TO_PGROUP_INDEX (0x00E42272),
 * PROC2_$UID_TO_UPID (0x00E40F6C), PROC2_$UPGID_TO_UID (0x00E4100C),
 * PROC2_$UNDEBUG (0x00E41810), PROC2_$STARTUP (0x00E73454).
 *
 * Pinned by the disassembly: SIGPAUSE saves +0x78 into +0x88, sets flags
 * 0x4000, waits on &FIM_$WIRED_DATA.quit_ec[AS_ID] with FIM_$WIRED_DATA.quit_value[AS_ID]+1 and
 * refreshes the value from the EC after each wake; SIGRETURN's onstack bit
 * is 0x0400 (high byte); SIGSETMASK returns the OLD mask; UID_TO_UPID's
 * zombie status; UPGID_TO_UID patches the low word of UID_$NIL.high;
 * UNDEBUG compares +0x26 with the caller's raw table index; STARTUP reads
 * the asid 8 bytes into the record it is handed.
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);
MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
uint16_t PROC1_$CURRENT, PROC1_$AS_ID;
uid_t UID_$NIL = { 0xAAAA5555u, 0x12345678u };
#include "fim/fim.h"
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_deliver, n_waitn, n_clear, n_set_asid, n_clear_super, n_set_valid, n_fim_startup;
static int16_t mock_find_index, mock_find_by_upgid; static status_$t mock_find_status;
static int16_t last_deliver, last_clear_idx; static int8_t last_clear_flag;
static ec_$eventcount_t *last_wait_ec; static int32_t last_wait_val;
static uint16_t last_asid; static void *last_fim_ctx;
static jmp_buf fault_jmp; static sigcontext_t **last_fr_ctx;

void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
int16_t PROC2_$FIND_INDEX(uid_t *u, status_$t *s) { (void)u; *s = mock_find_status; return mock_find_index; }
int16_t PGROUP_FIND_BY_UPGID(uint16_t g) { (void)g; return mock_find_by_upgid; }
void PROC2_$DELIVER_PENDING_INTERNAL(int16_t i) { n_deliver++; last_deliver = i; }
void DEBUG_CLEAR_INTERNAL(int16_t i, int8_t f) { n_clear++; last_clear_idx = i; last_clear_flag = f; }
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *vals, int16_t n)
{
    n_waitn++; last_wait_ec = ecs[0]; last_wait_val = vals[0]; (void)n;
    ecs[0]->value += 1;                                   /* the quit EC advanced */
    if (n_waitn == 2) P2_INFO_ENTRY(2)->sig_mask_2 = 0x8;  /* a signal arrives */
    return 0;
}
NORETURN void FIM_$FAULT_RETURN(sigcontext_t **c, uint32_t **r, void *f) { (void)r; (void)f; last_fr_ctx = c; longjmp(fault_jmp, 1); }
void PROC1_$SET_ASID(uint16_t a) { n_set_asid++; last_asid = a; }
void ACL_$CLEAR_SUPER(void) { n_clear_super++; }
void PROC2_$SET_VALID(void) { n_set_valid++; }
void FIM_$PROC2_STARTUP(void *c) { n_fim_startup++; last_fim_ctx = c; }

#include "proc2/sigpause.c"
#include "proc2/sigreturn.c"
#include "proc2/sigsetmask.c"
#include "proc2/uid_to_pgroup_index.c"
#include "proc2/uid_to_upid.c"
#include "proc2/upgid_to_uid.c"
#include "proc2/undebug.c"
#include "proc2/startup.c"

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
    memset(FIM_$WIRED_DATA.quit_ec, 0, sizeof(FIM_$WIRED_DATA.quit_ec)); memset(FIM_$WIRED_DATA.quit_value, 0, sizeof(FIM_$WIRED_DATA.quit_value));
    n_lock = n_unlock = n_deliver = n_waitn = n_clear = n_set_asid = n_clear_super = n_set_valid = n_fim_startup = 0;
    mock_find_index = 3; mock_find_status = status_$ok; mock_find_by_upgid = 0;
    PROC1_$CURRENT = 5; PROC1_$AS_ID = 3; PROC2_$DATA.pid_to_index[5] = 2;
    E(2)->self_index = 2; PROC2_$UNWIRED_DATA.info_alloc_ptr = 0;
}

TEST(sigpause_waits_then_delivers)
{
    uint32_t m = 0xF0, r[2];
    E(2)->sig_blocked_2 = 0x0F; E(2)->flags = 0x0400;
    FIM_$WIRED_DATA.quit_ec[3].value = 10; FIM_$WIRED_DATA.quit_value[3] = 10;
    PROC2_$SIGPAUSE(&m, r);
    ASSERT_EQ(E(2)->pad_88, 0x0F); ASSERT_EQ(E(2)->sig_blocked_2, 0xF0);
    ASSERT_EQ(E(2)->flags, 0x4400); ASSERT_EQ(r[0], 0xF0); ASSERT_EQ(r[1], 1);
    ASSERT_EQ(n_waitn, 2);
    ASSERT_EQ(last_wait_ec == &FIM_$WIRED_DATA.quit_ec[3], 1);
    ASSERT_EQ(last_wait_val, 12);                    /* refreshed value 11, + 1 */
    ASSERT_EQ(FIM_$WIRED_DATA.quit_value[3], 12);
    ASSERT_EQ(n_deliver, 1); ASSERT_EQ(last_deliver, 2);
    ASSERT_EQ(n_lock, 2); ASSERT_EQ(n_unlock, 2);
}

TEST(sigpause_immediate_when_already_pending)
{
    uint32_t m = 0, r[2];
    E(2)->sig_mask_2 = 1;
    PROC2_$SIGPAUSE(&m, r);
    ASSERT_EQ(n_waitn, 0); ASSERT_EQ(n_deliver, 1); ASSERT_EQ(r[1], 0);
}

TEST(sigreturn_high_byte_bit)
{
    sigcontext_t sc = { 0 }; sigcontext_t *scp = &sc; uint32_t r[2] = { 0, 0 };
    sc.sc_onstack = 1; sc.sc_mask = 0x33;
    E(2)->flags = 0x0004; E(2)->sig_mask_2 = 0x40;
    if (setjmp(fault_jmp) == 0) { PROC2_$SIGRETURN(&scp, NULL, NULL, r); }
    ASSERT_EQ(E(2)->flags, 0x0404); ASSERT_EQ(E(2)->sig_blocked_2, 0x33);
    ASSERT_EQ(n_deliver, 1); ASSERT_EQ(r[0], 0x33); ASSERT_EQ(r[1], 1);
    ASSERT_EQ(last_fr_ctx == &scp, 1);
    sc.sc_onstack = 0; E(2)->sig_mask_2 = 0;
    if (setjmp(fault_jmp) == 0) { PROC2_$SIGRETURN(&scp, NULL, NULL, r); }
    ASSERT_EQ(E(2)->flags, 0x0004); ASSERT_EQ(n_deliver, 1); ASSERT_EQ(r[1], 0);
    ASSERT_EQ(n_lock, n_unlock);
}

TEST(sigsetmask_old_value)
{
    uint32_t m = 0x0F, r[2];
    E(2)->sig_blocked_2 = 0xF0; E(2)->sig_mask_2 = 0x80;
    ASSERT_EQ(PROC2_$SIGSETMASK(&m, r), 0xF0);
    ASSERT_EQ(E(2)->sig_blocked_2, 0x0F); ASSERT_EQ(n_deliver, 1); ASSERT_EQ(r[0], 0x0F);
}

TEST(uid_to_pgroup_index_paths)
{
    uid_t syn = { 0x00001234u, 5 }, real = { 0x11001234u, 5 };
    mock_find_by_upgid = 6;
    ASSERT_EQ(PROC2_$UID_TO_PGROUP_INDEX(&syn), 6);
    E(3)->pgroup_table_idx = 9;
    ASSERT_EQ(PROC2_$UID_TO_PGROUP_INDEX(&real), 9);
    mock_find_status = status_$proc2_uid_not_found;
    ASSERT_EQ(PROC2_$UID_TO_PGROUP_INDEX(&real), 0);
}

TEST(uid_to_upid_paths)
{
    uid_t u = { 7, 8 }; uint16_t p = 0; status_$t st;
    PROC2_$UNWIRED_DATA.info_alloc_ptr = 2; E(2)->next_index = 3; E(3)->uid.high = 7; E(3)->uid.low = 8; E(3)->upid = 42;
    PROC2_$UID_TO_UPID(&u, &p, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(p, 42);
    E(3)->flags = PROC2_FLAG_ZOMBIE;
    PROC2_$UID_TO_UPID(&u, &p, &st);
    ASSERT_EQ(st, status_$proc2_zombie); ASSERT_EQ(p, 42);
    E(3)->uid.low = 9;
    PROC2_$UID_TO_UPID(&u, &p, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found);
    ASSERT_EQ(n_lock, n_unlock);
}

TEST(upgid_to_uid_patches_low_word)
{
    uint16_t g = 0xBEEF; uid_t out = { 0, 0 }; status_$t st = 1;
    PROC2_$UPGID_TO_UID(&g, &out, &st);
    ASSERT_EQ(out.high, 0xAAAABEEFu); ASSERT_EQ(out.low, 0x12345678u); ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 1);
}

TEST(undebug_paths)
{
    uid_t u = { 1, 2 }; status_$t st;
    E(3)->debugger_idx = 2;
    PROC2_$UNDEBUG(&u, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_clear, 1); ASSERT_EQ(last_clear_idx, 3); ASSERT_EQ(last_clear_flag, (int8_t)0xFF);
    E(3)->debugger_idx = 4;
    PROC2_$UNDEBUG(&u, &st);
    ASSERT_EQ(st, status_$proc2_proc_not_debug_target); ASSERT_EQ(n_clear, 1);
    mock_find_status = status_$proc2_zombie;
    PROC2_$UNDEBUG(&u, &st);
    ASSERT_EQ(st, status_$proc2_zombie);
}

TEST(startup_reads_asid_at_plus_8)
{
    startup_context_t ctx; ctx.user_data = 1; ctx.entry_point = 2; ctx.asid = 0x77;
    ctx.self_ptr = &ctx.user_data;
    PROC2_$STARTUP(ctx.self_ptr);
    ASSERT_EQ(n_set_asid, 1); ASSERT_EQ(last_asid, 0x77);
    ASSERT_EQ(n_clear_super, 1); ASSERT_EQ(n_set_valid, 1);
    ASSERT_EQ(n_fim_startup, 1); ASSERT_EQ(last_fim_ctx == ctx.self_ptr, 1);
}

int main(void)
{
    RUN_TEST(sigpause_waits_then_delivers);
    RUN_TEST(sigpause_immediate_when_already_pending);
    RUN_TEST(sigreturn_high_byte_bit);
    RUN_TEST(sigsetmask_old_value);
    RUN_TEST(uid_to_pgroup_index_paths);
    RUN_TEST(uid_to_upid_paths);
    RUN_TEST(upgid_to_uid_patches_low_word);
    RUN_TEST(undebug_paths);
    RUN_TEST(startup_reads_asid_at_plus_8);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
