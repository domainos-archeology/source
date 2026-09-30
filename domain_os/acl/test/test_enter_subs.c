/*
 * acl/test/test_enter_subs.c - unit tests for ACL_$ENTER_SUBS (0x00E46DA0)
 * and acl_$sids_allowed (0x00E44CE8)
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "acl/acl_internal.h"
#include "audit/audit.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"

MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);
uid_t UID_$NIL = { 0, 0 };
uid_t AUDIT_$ENTER_SUBS_EU = { 0x00040008u, 0 };
int8_t AUDIT_$ENABLED;
uint16_t PROC1_$CURRENT;

/* ---- mocks ----------------------------------------------------------- */

static uint32_t user_stack[2];
static int8_t setids_changed;
static status_$t setids_status;
static int8_t setids_set;
static uint16_t debugger_pid;
static int8_t suser;
static int n_audit;
static uint16_t audit_flag_seen;
static uint32_t audit_new_user_high;
static uint16_t audit_len_seen;

void *PROC1_$GET_USP(void) { return user_stack; }
void acl_$setids(uid_t *uid, int8_t set, uid_t *sids, uint32_t *owner_ext,
                 int8_t *changed, status_$t *status_ret)
{
    (void)uid; (void)owner_ext;
    setids_set = set;
    sids[0].high = 0x5151;                     /* a new user SID */
    *changed = setids_changed;
    *status_ret = setids_status;
}
uint16_t PROC2_$GET_DEBUGGER_PID(void) { return debugger_pid; }
int8_t acl_$check_suser_pid(int16_t pid) { (void)pid; return suser; }
void AUDIT_$LOG_EVENT_S(uid_t *event_uid, uint16_t *event_flags, void *sid,
                        status_$t *status, char *data, const uint16_t *data_len)
{
    (void)event_uid; (void)sid; (void)status;
    n_audit++; audit_flag_seen = *event_flags;
    audit_new_user_high = ((acl_sid_block_t *)(void *)data)->user_sid.high;
    audit_len_seen = *data_len;
}

#include "../sids_allowed.c"
#include "../enter_subs.c"

static void reset_state(void)
{
    memset(&ACL_$DATA, 0, sizeof(ACL_$DATA));
    memset(&ACL_$UNWIRED_DATA, 0, sizeof(ACL_$UNWIRED_DATA));
    user_stack[0] = 0xE0001234u;
    PROC1_$CURRENT = 3;
    AUDIT_$ENABLED = 0;
    setids_changed = (int8_t)0xFF;
    setids_status = 0;
    debugger_pid = 0;
    suser = 0;
    n_audit = 0;
    ACL_$DATA.current_sids[3].user_sid.high = 0x1111;
}

/* ---- acl_$sids_allowed ------------------------------------------------ */

static void test_sids_allowed(void)
{
    acl_sid_block_t s;
    memset(&s, 0, sizeof(s));
    ACL_$DATA.current_sids[5].user_sid.high = 7;
    ACL_$DATA.saved_sids[5].group_sid.high = 8;
    s.user_sid.high = 7;                        /* current */
    s.group_sid.high = 8;                       /* saved */
    ASSERT_EQ(0xFF, (uint8_t)acl_$sids_allowed(&s, 5));   /* login NIL */
    s.login_sid.high = 9;
    ASSERT_EQ(0, (uint8_t)acl_$sids_allowed(&s, 5));
    ACL_$DATA.original_sids[5].login_sid.high = 9;
    ASSERT_EQ(0xFF, (uint8_t)acl_$sids_allowed(&s, 5));
    s.org_sid.low = 1;                          /* neither current nor saved */
    ASSERT_EQ(0, (uint8_t)acl_$sids_allowed(&s, 5));
}

/* ---- ACL_$ENTER_SUBS -------------------------------------------------- */

static void test_manager_sets_magic_and_commits(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    PROC1_$CURRENT = 1;
    ASSERT_EQ(0xFF, (uint8_t)ACL_$ENTER_SUBS(&u, &st));
    ASSERT_EQ(0xE0001234u, (uint32_t)ACL_$UNWIRED_DATA.subs_magic);
    ASSERT_EQ(0xFF, (uint8_t)setids_set);
    ASSERT_EQ(0x5151, ACL_$DATA.current_sids[1].user_sid.high);
    ASSERT_EQ(0x5151, ACL_$DATA.saved_sids[1].user_sid.high);
}

static void test_untrusted_caller_does_not_commit(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    ACL_$UNWIRED_DATA.subs_magic = 0x42;
    ASSERT_EQ(0, (uint8_t)ACL_$ENTER_SUBS(&u, &st));
    ASSERT_EQ(0, (uint8_t)setids_set);
    ASSERT_EQ(0x1111, ACL_$DATA.current_sids[3].user_sid.high);
}

static void test_trusted_caller_commits(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    ACL_$UNWIRED_DATA.subs_magic = (int32_t)0xE0001234u;
    ASSERT_EQ(0xFF, (uint8_t)ACL_$ENTER_SUBS(&u, &st));
    ASSERT_EQ(0x5151, ACL_$DATA.current_sids[3].user_sid.high);
}

static void test_debugger_not_allowed(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    ACL_$UNWIRED_DATA.subs_magic = (int32_t)0xE0001234u;
    debugger_pid = 9;                           /* not suser, not allowed */
    AUDIT_$ENABLED = (int8_t)0xFF;
    ASSERT_EQ(0, (uint8_t)ACL_$ENTER_SUBS(&u, &st));
    ASSERT_EQ(0x00230001, st);
    ASSERT_EQ(0x1111, ACL_$DATA.current_sids[3].user_sid.high);
    ASSERT_EQ(1, n_audit);
    ASSERT_EQ(1, audit_flag_seen);
    ASSERT_EQ(0x1111, audit_new_user_high);     /* read back unchanged */
    ASSERT_EQ(0x24, audit_len_seen);
}

static void test_suser_debugger_ok(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    ACL_$UNWIRED_DATA.subs_magic = (int32_t)0xE0001234u;
    debugger_pid = 9;
    suser = (int8_t)0xFF;
    AUDIT_$ENABLED = (int8_t)0xFF;
    ASSERT_EQ(0xFF, (uint8_t)ACL_$ENTER_SUBS(&u, &st));
    ASSERT_EQ(1, n_audit);
    ASSERT_EQ(0, audit_flag_seen);
    ASSERT_EQ(0x5151, audit_new_user_high);
}

static void test_no_change_no_audit(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    setids_changed = 0;
    AUDIT_$ENABLED = (int8_t)0xFF;
    ASSERT_EQ(0, (uint8_t)ACL_$ENTER_SUBS(&u, &st));
    ASSERT_EQ(0, n_audit);
}

int main(void)
{
    printf("ACL_$ENTER_SUBS tests:\n");
    RUN_TEST(sids_allowed);
    RUN_TEST(manager_sets_magic_and_commits);
    RUN_TEST(untrusted_caller_does_not_commit);
    RUN_TEST(trusted_caller_commits);
    RUN_TEST(debugger_not_allowed);
    RUN_TEST(suser_debugger_ok);
    RUN_TEST(no_change_no_audit);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
