/*
 * xpd/test/test_registers.c - XPD_$GET_REGISTERS, XPD_$PUT_REGISTERS, the
 * FP helpers, XPD_$GET_FP/PUT_FP, XPD_$GET_TARGET_INFO (registers.c) and
 * XPD_$RESTART (restart.c).
 *
 * The debug-state record, its frame, registers and the two buffers live in
 * a VA arena so entry+0xC6 can hold a 32-bit address.
 */

#include <stdio.h>
#include <string.h>

#include "xpd/xpd_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-48s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
MODULE_DATA_DEFINE(proc2_$wired_data_t, PROC2_$WIRED_DATA, 0x00E2B978);
MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
MODULE_DATA_DEFINE(peb_globals_t, PEB_$INFO, 0x00E24C78);

static int lock_held;
void ML_$LOCK(int16_t id)   { (void)id; lock_held++; }
void ML_$UNLOCK(int16_t id) { (void)id; lock_held--; }

static int16_t find_index_result;
static status_$t find_index_status;
int16_t XPD_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret)
{
    (void)proc_uid;
    *status_ret = find_index_status;
    return find_index_result;
}

static uint16_t find_asid_result;
static status_$t find_asid_status;
uint16_t PROC2_$FIND_ASID(uid_t *proc_uid, int8_t *flag, status_$t *status_ret)
{
    (void)proc_uid; (void)flag;
    *status_ret = find_asid_status;
    return find_asid_result;
}

static int fim_get_calls, fim_put_calls, peb_load_calls, peb_unload_calls;
static void *fim_get_state, *fim_get_status, *peb_unload_state;
void FIM_$FP_GET_STATE(void *state, status_$t *status) { fim_get_calls++; fim_get_state = state; fim_get_status = status; }
void FIM_$FP_PUT_STATE(void *state, status_$t *status) { (void)state; (void)status; fim_put_calls++; }
void PEB_$LOAD_REGS(peb_fp_state_t *state) { (void)state; peb_load_calls++; }
void PEB_$UNLOAD_REGS(peb_fp_state_t *state) { peb_unload_calls++; peb_unload_state = state; }
static int fp_get_calls, fp_put_calls, peb_get_calls, peb_put_calls;
static uint16_t fp_get_asid;
static int16_t *peb_get_asid;
void (FP_$GET_FP)(uint32_t asid_slot) { uint16_t asid = (uint16_t)ARCH_PASCAL_SLOT_WORD(asid_slot); (void)asid; fp_get_calls++; fp_get_asid = asid; }
void (FP_$PUT_FP)(uint32_t asid_slot) { uint16_t asid = (uint16_t)ARCH_PASCAL_SLOT_WORD(asid_slot); (void)asid; (void)asid; fp_put_calls++; }
void PEB_$GET_FP(int16_t *asid) { peb_get_calls++; peb_get_asid = asid; }
void PEB_$PUT_FP(int16_t *asid) { (void)asid; peb_put_calls++; }

static int resume_calls;
static uint16_t resume_pid;
static status_$t resume_status;
void PROC1_$RESUME(uint16_t pid, status_$t *status_p) { resume_calls++; resume_pid = pid; *status_p = resume_status; }

static int32_t ec_value;
int32_t EC_$READ(ec_$eventcount_t *ec) { (void)ec; return ec_value; }
static int waitn_calls;
static int32_t waitn_val;
static int16_t waitn_n;
static ec_$eventcount_t *waitn_ec;
static proc2_info_t *waitn_stops;       /* the target stops after the wait */
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{
    waitn_calls++;
    waitn_ec = ecs[0]; waitn_val = *wait_val; waitn_n = num_ecs;
    ec_value++;
    if (waitn_stops != NULL) waitn_stops->flags |= XPD_PF_SUSPENDED;
    return 0;
}

#include "../xpd_data.c"
#include "../registers.c"
#include "../restart.c"

/* the VA arena */
static uint8_t arena[0x1000];
#define STATE_OFF 0x100
#define FRAME_OFF 0x200
#define REGS_OFF  0x300
#define FP_OFF    0x400
#define AUX_OFF   0x600
#define STATE ((xpd_$debug_state_t *)(arena + STATE_OFF))
#define FRAME ((xpd_$frame_t *)(arena + FRAME_OFF))
#define REGS  ((uint32_t *)(arena + REGS_OFF))
#define FP    ((uint32_t *)(arena + FP_OFF))
#define AUX   ((uint32_t *)(arena + AUX_OFF))

static proc2_info_t *entry;

static void reset(void)
{
    memset(PROC2_$DATA.info, 0, sizeof(PROC2_$DATA.info));
    memset(arena, 0, sizeof(arena));
    memset(XPD_$DATA, 0, sizeof(XPD_$DATA));
    PROC1_$CURRENT = 3;
    PROC1_$AS_ID = 5;
    find_index_result = 4;
    find_index_status = status_$ok;
    find_asid_result = 7;
    find_asid_status = status_$ok;
    entry = P2_INFO_ENTRY(4);
    entry->flags = XPD_PF_SUSPENDED | XPD_PF_DEBUG_TARGET;
    entry->level1_pid = 0x1F;
    entry->debugger_idx = 2;
    STATE->frame = FRAME;
    STATE->regs = REGS;
    STATE->fp_buf = FP;
    STATE->aux = AUX;
    STATE->fp_modified = 0;
    FRAME->sr = 0x2700;
    XPD_FRAME_PC_SET(FRAME, 0x00AB0000);
    XPD_ENTRY_STATE_VA_SET(entry, STATE_OFF);
    lock_held = 0;
    fim_get_calls = fim_put_calls = peb_load_calls = peb_unload_calls = 0;
    fp_get_calls = fp_put_calls = peb_get_calls = peb_put_calls = 0;
    resume_calls = 0; resume_status = status_$ok;
    ec_value = 10; waitn_calls = 0; waitn_stops = NULL;
    M68881_$SAVE_FLAG = 0; PEB_$INSTALLED_FLAG = 0;
}

/* ==========================================================================
 * GET_REGISTERS
 * ========================================================================== */

TEST(get_general)
{
    uid_t u = { 1, 1 };
    int16_t mode = XPD_REG_MODE_GENERAL;
    uint32_t out[16];
    status_$t st = 0x55;
    int i;
    reset();
    for (i = 0; i < 16; i++) REGS[i] = 0xD0000000u + i;
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(status_$ok, st);
    for (i = 0; i < 16; i++) ASSERT_EQ(0xD0000000u + i, out[i]);
    ASSERT_EQ(0, lock_held);
}

TEST(get_exception_short_and_long)
{
    uid_t u = { 1, 1 };
    int16_t mode = XPD_REG_MODE_EXCEPTION;
    uint32_t out[16];
    status_$t st = 0x55;
    reset();
    memset(out, 0xEE, sizeof(out));
    AUX[0] = 4;
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(0xC, out[0]);
    ASSERT_EQ(0x2700, out[1]);
    ASSERT_EQ(0x00AB0000, out[2]);
    ASSERT_EQ(0xEEEEEEEEu, out[3]);
    /* three words in the aux buffer */
    AUX[0] = 16; AUX[1] = 0x10; AUX[2] = 0x20; AUX[3] = 0x30;
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(0xC + 16 + 4, out[0]);
    ASSERT_EQ(0x60, out[3]);
    ASSERT_EQ(16, out[4]);
    ASSERT_EQ(0x10, out[5]); ASSERT_EQ(0x20, out[6]); ASSERT_EQ(0x30, out[7]);
}

TEST(get_fp_state_and_debug_state)
{
    uid_t u = { 1, 1 };
    int16_t mode = XPD_REG_MODE_FP_STATE;
    uint32_t out[16];
    status_$t st = 0x55;
    reset();
    FP[0] = 12; FP[1] = 0xF1; FP[2] = 0xF2;
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(12, out[0]); ASSERT_EQ(0xF1, out[1]); ASSERT_EQ(0xF2, out[2]);

    mode = XPD_REG_MODE_DEBUG_STATE;
    entry->pad_94 = 0x0609;
    PROC2_FAULT_PARAM_SET(entry, 0x00920015u);   /* 0x120015 with bit 23 */
    entry->sig_mask_2 = 0x77;
    XPD_ENTRY_LAST_PC_SET(entry, 0x1234);
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(0x00120015, out[0]);
    ASSERT_EQ(9, out[1]);
    ASSERT_EQ(0, out[2]);           /* the raw value is not 0x120015 */
    ASSERT_EQ(0x77, out[3]);
    PROC2_FAULT_PARAM_SET(entry, 0x00120015u);
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(0x1234, out[2]);
}

TEST(get_errors)
{
    uid_t u = { 1, 1 };
    int16_t mode = 4;
    uint32_t out[16];
    status_$t st = 0x55;
    reset();
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(status_$xpd_invalid_option, st);
    XPD_ENTRY_STATE_VA_SET(entry, 0);
    mode = 0;
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(status_$xpd_state_unavailable_for_this_event, st);
    find_index_status = status_$xpd_target_not_suspended;
    XPD_$GET_REGISTERS(&u, &mode, out, &st);
    ASSERT_EQ(status_$xpd_target_not_suspended, st);
    ASSERT_EQ(0, lock_held);
}

/* ==========================================================================
 * PUT_REGISTERS
 * ========================================================================== */

TEST(put_general_and_fp)
{
    uid_t u = { 1, 1 };
    int16_t mode = XPD_REG_MODE_GENERAL;
    uint32_t in[32];
    status_$t st = 0x55;
    int i;
    reset();
    for (i = 0; i < 16; i++) in[i] = 0xA0000000u + i;
    XPD_$PUT_REGISTERS(&u, &mode, in, &st);
    ASSERT_EQ(status_$ok, st);
    for (i = 0; i < 16; i++) ASSERT_EQ(0xA0000000u + i, REGS[i]);
    ASSERT_EQ(0, STATE->fp_modified);

    mode = XPD_REG_MODE_FP_STATE;
    in[0] = 12; in[1] = 0x11; in[2] = 0x22;
    XPD_$PUT_REGISTERS(&u, &mode, in, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(12, FP[0]); ASSERT_EQ(0x11, FP[1]); ASSERT_EQ(0x22, FP[2]);
    ASSERT_EQ(-1, STATE->fp_modified);
    in[0] = 4 + 0x6D * 4;               /* 0x6D longwords: one too many */
    XPD_$PUT_REGISTERS(&u, &mode, in, &st);
    ASSERT_EQ(status_$xpd_invalid_state_argument, st);
}

TEST(put_exception)
{
    uid_t u = { 1, 1 };
    int16_t mode = XPD_REG_MODE_EXCEPTION;
    uint32_t in[32];
    status_$t st = 0x55;
    reset();
    /* short form: aux length zeroed, SR/PC stored */
    in[0] = 0xC; in[1] = 0x00012000; in[2] = 0x00CD0002;
    AUX[0] = 99;
    XPD_$PUT_REGISTERS(&u, &mode, in, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, AUX[0]);
    ASSERT_EQ(-1, STATE->fp_modified);
    ASSERT_EQ(0x2000, FRAME->sr);
    ASSERT_EQ(0x00CD0002, XPD_FRAME_PC(FRAME));
    /* long form: the words go to aux, checked against their sum */
    in[0] = 0x20; in[3] = 0x33; in[4] = 12; in[5] = 0x11; in[6] = 0x22;
    XPD_$PUT_REGISTERS(&u, &mode, in, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(12, AUX[0]); ASSERT_EQ(0x11, AUX[1]); ASSERT_EQ(0x22, AUX[2]);
    in[3] = 0x34;
    XPD_$PUT_REGISTERS(&u, &mode, in, &st);
    ASSERT_EQ(status_$xpd_invalid_state_argument, st);
    mode = 3;
    XPD_$PUT_REGISTERS(&u, &mode, in, &st);
    ASSERT_EQ(status_$xpd_invalid_option, st);
}

/* ==========================================================================
 * FP helpers and GET_FP / PUT_FP / GET_TARGET_INFO
 * ========================================================================== */

TEST(fp_state_helpers)
{
    uint32_t buf[0x40], aux[4];
    reset();
    aux[0] = 99;
    XPD_$FP_GET_STATE(buf, aux);
    ASSERT_EQ(0, aux[0]);
    ASSERT_EQ(0, fim_get_calls); ASSERT_EQ(0, peb_unload_calls);
    M68881_$SAVE_FLAG = -1;
    XPD_$FP_GET_STATE(buf, aux);
    ASSERT_EQ(1, fim_get_calls);
    ASSERT_EQ((long long)(intptr_t)buf, (long long)(intptr_t)fim_get_state);
    ASSERT_EQ((long long)(intptr_t)aux, (long long)(intptr_t)fim_get_status);
    M68881_$SAVE_FLAG = 0; PEB_$INSTALLED_FLAG = -1;
    XPD_$FP_GET_STATE(buf, aux);
    ASSERT_EQ(1, peb_unload_calls);
    ASSERT_EQ((long long)(intptr_t)&buf[1], (long long)(intptr_t)peb_unload_state);
    ASSERT_EQ(0x20, buf[0]);
    ASSERT_EQ(4, aux[0]);
    XPD_$FP_PUT_STATE(buf, aux);
    ASSERT_EQ(1, peb_load_calls);
    M68881_$SAVE_FLAG = -1;
    XPD_$FP_PUT_STATE(buf, aux);
    ASSERT_EQ(1, fim_put_calls);
}

TEST(get_put_fp)
{
    uid_t u = { 1, 1 };
    status_$t st = 0x55;
    reset();
    XPD_DEBUGGER(2)->asid = 5;
    XPD_TARGET(7)->state = (2 << XPD_STATE_DEBUGGER_SHIFT) | XPD_STATE_ENABLED | (3 << XPD_STATE_EVENT_SHIFT);
    M68881_$SAVE_FLAG = -1;
    XPD_$GET_FP(&u, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, fp_get_calls);
    ASSERT_EQ(7, fp_get_asid);          /* the index FIND_ASID returned */
    M68881_$SAVE_FLAG = 0;
    XPD_$PUT_FP(&u, &st);
    ASSERT_EQ(1, peb_put_calls);
    /* the wrong debugger */
    XPD_DEBUGGER(2)->asid = 6;
    XPD_$GET_FP(&u, &st);
    ASSERT_EQ(status_$xpd_target_not_suspended, st);
    ASSERT_EQ(1, fp_get_calls);
    /* unknown process: FIND_ASID's status, no test */
    find_asid_result = 0; find_asid_status = 0x00190001;
    XPD_$GET_FP(&u, &st);
    ASSERT_EQ(0x00190001, st);
}

TEST(get_target_info)
{
    uid_t u = { 1, 1 };
    int8_t is_target = 5, is_susp = 5;
    status_$t st = 0x55;
    reset();
    XPD_TARGET(7)->state = XPD_STATE_ENABLED;
    XPD_$GET_TARGET_INFO(&u, &is_target, &is_susp, &st);
    ASSERT_EQ(0, is_target); ASSERT_EQ(0, is_susp);
    XPD_TARGET(7)->state |= (2 << XPD_STATE_DEBUGGER_SHIFT);
    XPD_$GET_TARGET_INFO(&u, &is_target, &is_susp, &st);
    ASSERT_EQ(-1, is_target); ASSERT_EQ(0, is_susp);
    XPD_TARGET(7)->state |= (1 << XPD_STATE_EVENT_SHIFT);
    XPD_$GET_TARGET_INFO(&u, &is_target, &is_susp, &st);
    ASSERT_EQ(-1, is_target); ASSERT_EQ(-1, is_susp);
}

/* ==========================================================================
 * RESTART
 * ========================================================================== */

TEST(restart_continue)
{
    uid_t u = { 1, 1 };
    uint16_t mode = XPD_RESTART_MODE_CONTINUE;
    int32_t pc = 1, status = 0;
    int16_t signal = 0;
    status_$t st = 0x55;
    reset();
    entry->flags = XPD_PF_SUSPENDED | XPD_PF_TRACE_PENDING | XPD_PF_DEBUG_TARGET;
    entry->pad_94 = 0;
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(XPD_PF_DEBUG_TARGET, entry->flags);
    ASSERT_EQ(1, resume_calls);
    ASSERT_EQ(0x1F, resume_pid);
    ASSERT_EQ(0x00AB0000, XPD_FRAME_PC(FRAME));    /* pc 1 keeps it */
    ASSERT_EQ(0, PROC2_FAULT_PARAM_GET(entry));    /* signal unchanged */
    ASSERT_EQ(0, waitn_calls);
}

TEST(restart_step_modes_and_signal)
{
    uid_t u = { 1, 1 };
    uint16_t mode = XPD_RESTART_MODE_STEP;
    int32_t pc = 0x00CC0000, status = 0x00120019;
    int16_t signal = 9;
    status_$t st = 0x55;
    reset();
    entry->pad_94 = 0;
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(0x00CC0000, XPD_FRAME_PC(FRAME));
    ASSERT_EQ(0x00920019u, PROC2_FAULT_PARAM_GET(entry));  /* bit 23 set */
    ASSERT_EQ(9, entry->pad_94);
    ASSERT_EQ(XPD_PF_TRACE_PENDING | XPD_PF_DEBUG_TARGET, entry->flags);
    /* mode 3: only suspended cleared */
    reset();
    entry->flags = XPD_PF_SUSPENDED | XPD_PF_DEBUG_TARGET;
    mode = XPD_RESTART_MODE_STEP_NO_TRACE;
    signal = 0; status = 0; pc = 1;
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(XPD_PF_DEBUG_TARGET, entry->flags);
    ASSERT_EQ(1, resume_calls);
    /* signal 0x13 with a status: stored even when the signal is unchanged */
    reset();
    entry->pad_94 = 0x13; signal = 0x13; status = 0x00160015; mode = 1;
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(0x00960015u, PROC2_FAULT_PARAM_GET(entry));
    /* mode 0: nothing resumed, ok; mode 4: nothing resumed either */
    reset(); mode = 0;
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(status_$ok, st); ASSERT_EQ(0, resume_calls);
    ASSERT_EQ(XPD_PF_SUSPENDED | XPD_PF_DEBUG_TARGET, entry->flags);
    mode = 4;
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(status_$ok, st); ASSERT_EQ(0, resume_calls);
}

TEST(restart_waits_for_the_next_stop)
{
    uid_t u = { 1, 1 };
    uint16_t mode = XPD_RESTART_MODE_CONTINUE;
    int32_t pc = 1, status = 0;
    int16_t signal = 0;
    status_$t st = 0x55;
    reset();
    XPD_PTRACE_OPTS(entry)->flags2 = 0x10;
    waitn_stops = entry;
    entry->pad_94 = 0x0007;
    signal = 7;                         /* unchanged: no status rewrite */
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(1, waitn_calls);
    ASSERT_EQ((long long)(intptr_t)PROC_CR_REC_EC(2), (long long)(intptr_t)waitn_ec);
    ASSERT_EQ(11, waitn_val);
    ASSERT_EQ(1, waitn_n);
    ASSERT_EQ(XPD_PF_STATE_SAVED, entry->flags & XPD_PF_STATE_SAVED);
    ASSERT_EQ(0x09010000 + 7, st);                /* no fault status: 0x0901<signal> */
    /* with a fault status recorded by the next stop */
    reset();
    XPD_PTRACE_OPTS(entry)->flags2 = 0x10;
    waitn_stops = entry;
    PROC2_FAULT_PARAM_SET(entry, 0x00120015u);
    signal = 0; entry->pad_94 = 0;
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(1, waitn_calls);
    ASSERT_EQ(0x00120015, st);
    /* the target went away */
    reset();
    XPD_PTRACE_OPTS(entry)->flags2 = 0x10;
    entry->flags = XPD_PF_SUSPENDED;          /* not a debug target */
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(status_$proc2_uid_not_found, st);
}

TEST(restart_errors)
{
    uid_t u = { 1, 1 };
    uint16_t mode = 1;
    int32_t pc = 1, status = 0;
    int16_t signal = 0;
    status_$t st = 0x55;
    reset();
    XPD_ENTRY_STATE_VA_SET(entry, 0);
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(status_$xpd_state_unavailable_for_this_event, st);
    find_index_status = status_$xpd_target_not_suspended;
    XPD_$RESTART(&u, &mode, &pc, &signal, &status, &st);
    ASSERT_EQ(status_$xpd_target_not_suspended, st);
    ASSERT_EQ(0, lock_held);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    printf("XPD register / restart tests\n");
    RUN_TEST(get_general);
    RUN_TEST(get_exception_short_and_long);
    RUN_TEST(get_fp_state_and_debug_state);
    RUN_TEST(get_errors);
    RUN_TEST(put_general_and_fp);
    RUN_TEST(put_exception);
    RUN_TEST(fp_state_helpers);
    RUN_TEST(get_put_fp);
    RUN_TEST(get_target_info);
    RUN_TEST(restart_continue);
    RUN_TEST(restart_step_modes_and_signal);
    RUN_TEST(restart_waits_for_the_next_stop);
    RUN_TEST(restart_errors);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
