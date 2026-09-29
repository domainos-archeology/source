/*
 * acl/test/test_set_all_sids.c - unit tests for ACL_$SET_RE_ALL_SIDS
 * (0x00E481AE) and ACL_$SET_RES_ALL_SIDS (0x00E4855A).
 *
 * Both .c files are #included below and driven through mocks of
 * acl_$check_suser_pid, ACL_$ADD_PROJ / ACL_$DELETE_PROJ and
 * AUDIT_$LOG_EVENT_S.  The tests pin down the audit snapshot/log path the
 * two routines used to be missing entirely (beads source-92fw, source-xx9y):
 * the constant length cells, the audit call happening when AUDIT_$ENABLED is
 * 0xFF and not happening when it is 0, and the failed super-user check
 * branching into the audit tail rather than returning.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "acl/acl_internal.h"
#include "audit/audit.h"

/* ------------------------------------------------------------------ */
/* Globals the two routines reach through A5 / absolute addresses       */
/* ------------------------------------------------------------------ */

uint16_t PROC1_$CURRENT;

MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);

int8_t AUDIT_$ENABLED;
uid_t  AUDIT_$SET_SID_EU = { 0x00040007u, 0x00000000u };

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static int8_t suser_result;          /* what acl_$check_suser_pid returns */
static int16_t suser_pid_seen;

int8_t acl_$check_suser_pid(int16_t pid)
{
    suser_pid_seen = pid;
    return suser_result;
}

static int   add_proj_calls;
static int   del_proj_calls;
static uid_t add_proj_uid;
static uid_t del_proj_uid;
static int16_t super_count_during_proj;

void ACL_$ADD_PROJ(uid_t *proj_acl, status_$t *status_ret)
{
    add_proj_calls++;
    add_proj_uid = *proj_acl;
    super_count_during_proj = ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT];
    (void)status_ret;
}

void ACL_$DELETE_PROJ(uid_t *proj_acl, status_$t *status_ret)
{
    del_proj_calls++;
    del_proj_uid = *proj_acl;
    (void)status_ret;
}

/* AUDIT_$LOG_EVENT_S capture. */
static int       log_calls;
static uid_t     log_event_uid;
static uint16_t  log_flag;
static uint16_t  log_len;
static status_$t log_status;
static uint8_t   log_data[0x100];
static long      log_sid_offset;     /* sid - data, in bytes */

void AUDIT_$LOG_EVENT_S(uid_t *event_uid, uint16_t *event_flags,
                        void *sid, status_$t *status,
                        char *data, const uint16_t *data_len)
{
    log_calls++;
    log_event_uid = *event_uid;
    log_flag      = *event_flags;
    log_status    = *status;
    log_len       = *data_len;
    log_sid_offset = (long)((const uint8_t *)sid - (const uint8_t *)data);
    memcpy(log_data, data, log_len);
}

/* ------------------------------------------------------------------ */
/* The routines under test                                             */
/* ------------------------------------------------------------------ */

#include "../set_re_all_sids.c"
#include "../set_res_all_sids.c"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

#define TEST_PID 3

static void fill_block(acl_sid_block_t *b, uint32_t seed)
{
    uint32_t *p = (uint32_t *)b;
    int i;
    for (i = 0; i < 9; i++) {
        p[i] = seed + (uint32_t)i;
    }
}

static void reset_all(int8_t audit_enabled, int8_t suser)
{
    memset(ACL_$DATA.original_sids, 0, sizeof(ACL_$DATA.original_sids));
    memset(ACL_$DATA.current_sids, 0, sizeof(ACL_$DATA.current_sids));
    memset(ACL_$DATA.saved_sids, 0, sizeof(ACL_$DATA.saved_sids));
    memset(ACL_$DATA.saved_proj, 0, sizeof(ACL_$DATA.saved_proj));
    memset(ACL_$DATA.proj_lists, 0, sizeof(ACL_$DATA.proj_lists));
    memset(ACL_$UNWIRED_DATA.super_count, 0, sizeof(ACL_$UNWIRED_DATA.super_count));
    memset(log_data, 0, sizeof(log_data));

    PROC1_$CURRENT = TEST_PID;
    AUDIT_$ENABLED = audit_enabled;
    suser_result   = suser;
    suser_pid_seen = -1;
    add_proj_calls = del_proj_calls = 0;
    super_count_during_proj = -1;
    log_calls = 0;
    log_flag = 0xFFFF;
    log_len = 0;
    log_status = 0x7F7F7F7F;
    log_sid_offset = -1;
}

/* ------------------------------------------------------------------ */
/* The `pea (d,PC)` constant cells                                     */
/* ------------------------------------------------------------------ */

TEST(constant_cells)
{
    /* 0x00E48558: 00 90 */
    ASSERT_EQ(0x0090, acl_$set_re_sids_audit_len);
    /* 0x00E48790: 00 d8 */
    ASSERT_EQ(0x00D8, acl_$set_res_sids_audit_len);

    /* The lengths are exactly the audit records the two build in frame. */
    ASSERT_EQ(0x0090, sizeof(acl_$set_re_sids_audit_t));
    ASSERT_EQ(0x00D8, sizeof(acl_$set_res_sids_audit_t));
    ASSERT_EQ(0x24, sizeof(acl_sid_block_t));
}

/* ------------------------------------------------------------------ */
/* ACL_$SET_RE_ALL_SIDS                                                */
/* ------------------------------------------------------------------ */

/* AUDIT_$ENABLED == 0: the whole snapshot/log path is skipped. */
TEST(re_audit_disabled_never_logs)
{
    acl_sid_block_t new_orig, new_curr;
    acl_proj_list_t sproj = { 1, 2, 3 }, cproj = { 4, 5, 6 };
    status_$t status = 0x11111111;

    reset_all(0, (int8_t)-1);            /* auditing off, caller is suser */
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);

    ACL_$SET_RE_ALL_SIDS(&new_orig, &new_curr, &sproj, &cproj, &status);

    ASSERT_EQ(0, log_calls);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x1000, ((uint32_t *)&ACL_$DATA.original_sids[TEST_PID])[0]);
    ASSERT_EQ(0x2000, ((uint32_t *)&ACL_$DATA.current_sids[TEST_PID])[0]);
    /* 0x00E4845C: the ORIGINAL block changed, so SAVED took the NEW one. */
    ASSERT_EQ(0x1000, ((uint32_t *)&ACL_$DATA.saved_sids[TEST_PID])[0]);
    ASSERT_EQ(1, ACL_$DATA.saved_proj[TEST_PID].field_00);
    ASSERT_EQ(4, ACL_$DATA.proj_lists[TEST_PID].field_00);
}

/* AUDIT_$ENABLED == 0xFF and something changed: one log, flag 0. */
TEST(re_audit_enabled_logs_success)
{
    acl_sid_block_t new_orig, new_curr;
    acl_proj_list_t sproj = { 0, 0, 0 }, cproj = { 0, 0, 0 };
    status_$t status = 0x11111111;
    const acl_$set_re_sids_audit_t *rec;

    reset_all((int8_t)0xFF, (int8_t)-1);
    fill_block(&ACL_$DATA.original_sids[TEST_PID], 0x0100);
    fill_block(&ACL_$DATA.current_sids[TEST_PID], 0x0200);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);

    ACL_$SET_RE_ALL_SIDS(&new_orig, &new_curr, &sproj, &cproj, &status);

    ASSERT_EQ(1, log_calls);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, log_flag);                    /* 0x00E484C0: clr.w */
    ASSERT_EQ(0x0090, log_len);
    ASSERT_EQ(0x00040007u, log_event_uid.high);
    /* `pea (-0x6c,A6)` is 0x24 bytes into the `pea (-0x90,A6)` block. */
    ASSERT_EQ(0x24, log_sid_offset);

    rec = (const acl_$set_re_sids_audit_t *)log_data;
    ASSERT_EQ(0x0100, ((const uint32_t *)&rec->old_original)[0]);
    ASSERT_EQ(0x0200, ((const uint32_t *)&rec->old_current)[0]);
    ASSERT_EQ(0x1000, ((const uint32_t *)&rec->new_original)[0]);
    ASSERT_EQ(0x2000, ((const uint32_t *)&rec->new_current)[0]);
}

/* A non-super-user with an unrelated SID is refused - and still audited. */
TEST(re_refusal_falls_into_audit_tail)
{
    acl_sid_block_t new_orig, new_curr;
    acl_proj_list_t sproj = { 0, 0, 0 }, cproj = { 0, 0, 0 };
    status_$t status = 0;

    reset_all((int8_t)0xFF, 0);          /* auditing on, NOT a super-user */
    fill_block(&ACL_$DATA.original_sids[TEST_PID], 0x0100);
    fill_block(&ACL_$DATA.current_sids[TEST_PID], 0x0200);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);

    ACL_$SET_RE_ALL_SIDS(&new_orig, &new_curr, &sproj, &cproj, &status);

    ASSERT_EQ(TEST_PID, suser_pid_seen);
    ASSERT_EQ(status_$no_right_to_perform_operation, status);
    /* Nothing was written. */
    ASSERT_EQ(0x0100, ((uint32_t *)&ACL_$DATA.original_sids[TEST_PID])[0]);
    /* 0x00E48282 jumps to 0x00E484C4, the audit tail, not to the epilogue. */
    ASSERT_EQ(1, log_calls);
    ASSERT_EQ(1, log_flag);              /* 0x00E481D8: move.w #0x1 */
    ASSERT_EQ(status_$no_right_to_perform_operation, log_status);
    ASSERT_EQ(0x0090, log_len);
}

/* Same refusal with auditing off: no log at all. */
TEST(re_refusal_audit_disabled)
{
    acl_sid_block_t new_orig, new_curr;
    acl_proj_list_t sproj = { 0, 0, 0 }, cproj = { 0, 0, 0 };
    status_$t status = 0;

    reset_all(0, 0);
    fill_block(&ACL_$DATA.original_sids[TEST_PID], 0x0100);
    fill_block(&ACL_$DATA.current_sids[TEST_PID], 0x0200);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);

    ACL_$SET_RE_ALL_SIDS(&new_orig, &new_curr, &sproj, &cproj, &status);

    ASSERT_EQ(status_$no_right_to_perform_operation, status);
    ASSERT_EQ(0, log_calls);
}

/* A super-user setting the values already in place logs nothing. */
TEST(re_no_change_no_log)
{
    acl_sid_block_t new_orig, new_curr;
    acl_proj_list_t sproj = { 0, 0, 0 }, cproj = { 0, 0, 0 };
    status_$t status = 0x11111111;

    reset_all((int8_t)0xFF, (int8_t)-1);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);
    ACL_$DATA.original_sids[TEST_PID] = new_orig;
    ACL_$DATA.current_sids[TEST_PID]  = new_curr;

    ACL_$SET_RE_ALL_SIDS(&new_orig, &new_curr, &sproj, &cproj, &status);

    ASSERT_EQ(status_$ok, status);
    /* 0x00E4850E: both blocks match and the status is 0 -> plain return. */
    ASSERT_EQ(0, log_calls);
    /* 0x00E4845C: nothing changed, so SAVED was left alone. */
    ASSERT_EQ(0, ((uint32_t *)&ACL_$DATA.saved_sids[TEST_PID])[0]);
}

/* The group-SID move brackets the two project calls with SUPER_COUNT++. */
TEST(re_group_change_bumps_super_count)
{
    acl_sid_block_t new_orig, new_curr;
    acl_proj_list_t sproj = { 0, 0, 0 }, cproj = { 0, 0, 0 };
    status_$t status = 0;

    reset_all(0, (int8_t)-1);
    fill_block(&ACL_$DATA.original_sids[TEST_PID], 0x0100);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);

    ACL_$SET_RE_ALL_SIDS(&new_orig, &new_curr, &sproj, &cproj, &status);

    ASSERT_EQ(1, del_proj_calls);
    ASSERT_EQ(1, add_proj_calls);
    ASSERT_EQ(1, super_count_during_proj);          /* 0x00E48430: addq.w #1 */
    ASSERT_EQ(0, ACL_$UNWIRED_DATA.super_count[TEST_PID]);       /* 0x00E48458: subq.w #1 */
    /* The deleted UID is the OLD original group SID (offset 8 = words 2,3). */
    ASSERT_EQ(0x0102, del_proj_uid.high);
    ASSERT_EQ(0x1002, add_proj_uid.high);
}

/* ------------------------------------------------------------------ */
/* ACL_$SET_RES_ALL_SIDS                                               */
/* ------------------------------------------------------------------ */

TEST(res_audit_enabled_logs_success)
{
    acl_sid_block_t new_orig, new_curr, new_save;
    acl_proj_list_t sproj = { 7, 8, 9 }, cproj = { 10, 11, 12 };
    status_$t status = 0x11111111;
    const acl_$set_res_sids_audit_t *rec;

    reset_all((int8_t)0xFF, (int8_t)-1);
    fill_block(&ACL_$DATA.original_sids[TEST_PID], 0x0100);
    fill_block(&ACL_$DATA.current_sids[TEST_PID], 0x0200);
    fill_block(&ACL_$DATA.saved_sids[TEST_PID], 0x0300);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);
    fill_block(&new_save, 0x3000);

    ACL_$SET_RES_ALL_SIDS(&new_orig, &new_curr, &new_save, &sproj, &cproj,
                          &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, log_calls);
    ASSERT_EQ(0, log_flag);
    ASSERT_EQ(0x00D8, log_len);
    /* `pea (-0xb4,A6)` is 0x24 bytes into the `pea (-0xd8,A6)` block. */
    ASSERT_EQ(0x24, log_sid_offset);

    rec = (const acl_$set_res_sids_audit_t *)log_data;
    ASSERT_EQ(0x0100, ((const uint32_t *)&rec->old_original)[0]);
    ASSERT_EQ(0x0200, ((const uint32_t *)&rec->old_current)[0]);
    ASSERT_EQ(0x0300, ((const uint32_t *)&rec->old_saved)[0]);
    ASSERT_EQ(0x1000, ((const uint32_t *)&rec->new_original)[0]);
    ASSERT_EQ(0x2000, ((const uint32_t *)&rec->new_current)[0]);
    ASSERT_EQ(0x3000, ((const uint32_t *)&rec->new_saved)[0]);

    ASSERT_EQ(7,  ACL_$DATA.saved_proj[TEST_PID].field_00);
    ASSERT_EQ(10, ACL_$DATA.proj_lists[TEST_PID].field_00);
}

/* 0x00E4861E: `bpl.w 0x00E486DA` - the refusal reaches the audit tail. */
TEST(res_non_suser_is_audited)
{
    acl_sid_block_t new_orig, new_curr, new_save;
    acl_proj_list_t sproj = { 0, 0, 0 }, cproj = { 0, 0, 0 };
    status_$t status = 0;

    reset_all((int8_t)0xFF, 0);
    fill_block(&ACL_$DATA.original_sids[TEST_PID], 0x0100);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);
    fill_block(&new_save, 0x3000);

    ACL_$SET_RES_ALL_SIDS(&new_orig, &new_curr, &new_save, &sproj, &cproj,
                          &status);

    ASSERT_EQ(status_$no_right_to_perform_operation, status);
    ASSERT_EQ(0x0100, ((uint32_t *)&ACL_$DATA.original_sids[TEST_PID])[0]);
    ASSERT_EQ(0, add_proj_calls);
    ASSERT_EQ(1, log_calls);
    ASSERT_EQ(1, log_flag);
    ASSERT_EQ(status_$no_right_to_perform_operation, log_status);
}

TEST(res_non_suser_audit_disabled)
{
    acl_sid_block_t new_orig, new_curr, new_save;
    acl_proj_list_t sproj = { 0, 0, 0 }, cproj = { 0, 0, 0 };
    status_$t status = 0;

    reset_all(0, 0);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);
    fill_block(&new_save, 0x3000);

    ACL_$SET_RES_ALL_SIDS(&new_orig, &new_curr, &new_save, &sproj, &cproj,
                          &status);

    ASSERT_EQ(status_$no_right_to_perform_operation, status);
    ASSERT_EQ(0, log_calls);
}

TEST(res_no_change_no_log)
{
    acl_sid_block_t new_orig, new_curr, new_save;
    acl_proj_list_t sproj = { 0, 0, 0 }, cproj = { 0, 0, 0 };
    status_$t status = 0x11111111;

    reset_all((int8_t)0xFF, (int8_t)-1);
    fill_block(&new_orig, 0x1000);
    fill_block(&new_curr, 0x2000);
    fill_block(&new_save, 0x3000);
    ACL_$DATA.original_sids[TEST_PID] = new_orig;
    ACL_$DATA.current_sids[TEST_PID]  = new_curr;
    ACL_$DATA.saved_sids[TEST_PID]    = new_save;

    ACL_$SET_RES_ALL_SIDS(&new_orig, &new_curr, &new_save, &sproj, &cproj,
                          &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, log_calls);
}

int main(void)
{
    printf("ACL_$SET_RE_ALL_SIDS / ACL_$SET_RES_ALL_SIDS tests\n");

    RUN_TEST(constant_cells);

    RUN_TEST(re_audit_disabled_never_logs);
    RUN_TEST(re_audit_enabled_logs_success);
    RUN_TEST(re_refusal_falls_into_audit_tail);
    RUN_TEST(re_refusal_audit_disabled);
    RUN_TEST(re_no_change_no_log);
    RUN_TEST(re_group_change_bumps_super_count);

    RUN_TEST(res_audit_enabled_logs_success);
    RUN_TEST(res_non_suser_is_audited);
    RUN_TEST(res_non_suser_audit_disabled);
    RUN_TEST(res_no_change_no_log);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
