/*
 * ast/test/test_process_aote.c - Unit tests for ast_$process_aote (0x00E01AD2)
 *                                 and ast_$purify_aote (0x00E013A0)
 *
 * The test #includes both .c files directly and drives them through
 * mocks.  It pins for process_aote:
 *
 *   - refusal for in-transition / referenced / protected type-2 objects
 *     (and that `keep` lifts the type-2 refusal);
 *   - the ASTE drain with the (purge, keep) bytes, the wait on an
 *     in-transition ASTE only when `wait` is TRUE;
 *   - purify skipped when `purge`; the unlink hashes obj_loc_uid;
 *   - failure statuses: "not deactivatable" passed through, others get
 *     bit 31; in-transition cleared and waiters woken only on failure.
 *
 * and for purify_aote:
 *
 *   - the remote path tests flags bit 4 (not status_flags) and copies
 *     the DTU from record +0x24/+0x28;
 *   - the local path stamps DTU on TOUCHED, writes on DIRTY, swallows
 *     "disk write protected" and re-dirties on other failures.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/process_aote.c"
#include "ast/purify_aote.c"

#define TEST_BUCKETS 251
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

static aote_t test_aote, other_aote;
static aste_t s1, s2;

static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static uint32_t hash_result; static uid_t *hash_uid; static uint16_t hash_size;
uint32_t UID_$HASH(uid_t *uid, uint16_t *size) { hash_uid = uid; hash_size = *size; return hash_result; }

static int wait_calls; static aste_t *wait_clear;
void AST_$WAIT_FOR_AST_INTRANS(void)
{
    wait_calls++;
    if (wait_clear) { wait_clear->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS; }
}

static int deact_calls; static int8_t deact_purge, deact_keep; static status_$t deact_status; static aste_t *deact_fail;
void AST_$DEACTIVATE_SEGMENT(aste_t *aste, int8_t purge, int8_t keep, status_$t *status)
{
    deact_calls++; deact_purge = purge; deact_keep = keep;
    *status = (aste == deact_fail) ? deact_status : status_$ok;
}

static int free_calls;
void AST_$FREE_ASTE(aste_t *aste) { free_calls++; test_aote.aste_list = aste->next; }

static int advance_calls;
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advance_calls++; }

/* purify_aote's callees */
static int getinfo_calls; static uint16_t getinfo_flags; static status_$t getinfo_status;
void NETWORK_$AST_GET_INFO(void *uid_info, uint16_t *flags, void *attrs, status_$t *status)
{
    (void)uid_info;
    getinfo_calls++; getinfo_flags = *flags;
    ((uint32_t *)attrs)[0x24 / 4] = 0xAABBCCDD;
    ((uint32_t *)attrs)[0x28 / 4] = 0xEEFF0000;
    *status = getinfo_status;
}
static int clock_calls; static clock_t *clock_dst;
void TIME_$CLOCK(clock_t *c) { clock_calls++; clock_dst = c; c->high = 0x11223344; c->low = 0x5566; }
static int write_calls; static char write_flags; static status_$t write_status; static uint8_t write_first;
void VTOCE_$WRITE(vtoc_$lookup_req_t *req, vtoce_$result_t *data, char flags, status_$t *status)
{
    (void)req;
    write_calls++; write_flags = flags; write_first = ((uint8_t *)data)[0];   /* aote+0x0C */
    *status = write_status;
}

static void reset_state(void)
{
    memset(AST_$DATA.aoth, 0, sizeof(AST_$DATA.aoth));
    memset(&test_aote, 0, sizeof(test_aote)); memset(&other_aote, 0, sizeof(other_aote));
    memset(&s1, 0, sizeof(s1)); memset(&s2, 0, sizeof(s2));
    lock_calls = unlock_calls = 0;
    hash_result = 3; hash_uid = NULL; hash_size = 0;
    wait_calls = 0; wait_clear = NULL;
    deact_calls = 0; deact_status = status_$ok; deact_fail = NULL;
    free_calls = 0; advance_calls = 0;
    getinfo_calls = 0; getinfo_status = status_$ok;
    clock_calls = 0; clock_dst = NULL;
    write_calls = 0; write_status = status_$ok;
}

TEST(refusals)
{
    status_$t status;

    test_aote.flags = AOTE_FLAG_IN_TRANS;
    ast_$process_aote(&test_aote, 0, 0, 0, &status);
    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);

    test_aote.flags = 0; test_aote.ref_count = 1;
    ast_$process_aote(&test_aote, 0, 0, 0, &status);
    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);

    /* protected local type-2 */
    test_aote.ref_count = 0; test_aote.sub_type = 2; test_aote.attr_flags_lo = 0x02;
    ast_$process_aote(&test_aote, 0, 0, 0, &status);
    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);
    ASSERT_EQ(0, test_aote.flags);                     /* never marked */

    /* keep = TRUE lifts it (the AOTE must be on its bucket for the unlink) */
    AST_$DATA.aoth[3] = &test_aote;
    ast_$process_aote(&test_aote, -1, -1, 0, &status);
    ASSERT_EQ(status_$ok, status);

    /* a remote protected type-2 is not refused */
    reset_state();
    test_aote.sub_type = 2; test_aote.attr_flags_lo = 0x02; test_aote.remote_flag = (int8_t)0x80;
    AST_$DATA.aoth[3] = &test_aote;
    ast_$process_aote(&test_aote, -1, 0, 0, &status);
    ASSERT_EQ(status_$ok, status);
}

TEST(drain_and_unlink)
{
    status_$t status;

    test_aote.obj_loc_uid.high = 0x77;
    s1.next = &s2; test_aote.aste_list = &s1;
    other_aote.hash_next = &test_aote;
    AST_$DATA.aoth[3] = &other_aote;
    test_aote.hash_next = NULL;

    ast_$process_aote(&test_aote, -1, 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(2, deact_calls);
    ASSERT_EQ(0xFF, (uint8_t)deact_purge);
    ASSERT_EQ(0, deact_keep);
    ASSERT_EQ(2, free_calls);
    ASSERT_EQ(0, write_calls);                         /* purge: no purify */
    ASSERT_EQ((uintptr_t)&test_aote.obj_loc_uid, (uintptr_t)hash_uid);
    ASSERT_EQ(0x00FB, hash_size);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)other_aote.hash_next);
    ASSERT_EQ(AOTE_FLAG_IN_TRANS, test_aote.flags);    /* left set */
    ASSERT_EQ(0, advance_calls);
}

TEST(head_unlink_and_purify)
{
    status_$t status;

    AST_$DATA.aoth[3] = &test_aote; test_aote.hash_next = &other_aote;
    test_aote.flags = AOTE_FLAG_DIRTY;

    ast_$process_aote(&test_aote, 0, 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, write_calls);                         /* purify ran */
    ASSERT_EQ(0, write_flags);
    ASSERT_EQ((uintptr_t)&other_aote, (uintptr_t)AST_$DATA.aoth[3]);
}

TEST(wait_for_in_transition_aste)
{
    status_$t status;

    s1.flags = ASTE_FLAG_IN_TRANS; test_aote.aste_list = &s1;
    wait_clear = &s1;
    AST_$DATA.aoth[3] = &test_aote;

    ast_$process_aote(&test_aote, -1, 0, -1, &status);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(1, deact_calls);

    /* wait = FALSE: deactivate at once */
    reset_state();
    s1.flags = ASTE_FLAG_IN_TRANS; test_aote.aste_list = &s1;
    AST_$DATA.aoth[3] = &test_aote;
    ast_$process_aote(&test_aote, -1, 0, 0, &status);
    ASSERT_EQ(0, wait_calls);
    ASSERT_EQ(1, deact_calls);
}

TEST(deactivate_failures)
{
    status_$t status;

    AST_$DATA.aoth[3] = &test_aote;
    test_aote.aste_list = &s1;
    deact_fail = &s1; deact_status = status_$ast_segment_not_deactivatable;
    ast_$process_aote(&test_aote, -1, 0, 0, &status);
    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);   /* passed through */
    ASSERT_EQ(0, test_aote.flags);
    ASSERT_EQ(1, advance_calls);

    reset_state();
    AST_$DATA.aoth[3] = &test_aote;
    test_aote.aste_list = &s1;
    deact_fail = &s1; deact_status = 0x00050007;
    ast_$process_aote(&test_aote, -1, 0, 0, &status);
    ASSERT_EQ(0x80050007u, (uint32_t)status);
    ASSERT_EQ(0, test_aote.flags);
    ASSERT_EQ(1, advance_calls);
}

TEST(purify_aote_remote)
{
    status_$t status = 0x99;

    test_aote.remote_flag = (int8_t)0x80;
    test_aote.flags = AOTE_FLAG_TOUCHED;
    test_aote.status_flags = 0;

    ast_$purify_aote(&test_aote, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, getinfo_calls);
    ASSERT_EQ(0x0080, getinfo_flags);
    ASSERT_EQ(0, test_aote.flags);
    ASSERT_EQ(0xAABBCCDD, test_aote.dtu_high);
    ASSERT_EQ(0xEEFF, test_aote.dtu_low);
    ASSERT_EQ(2, unlock_calls); ASSERT_EQ(2, lock_calls);   /* AST + PMAP */

    /* failure restores TOUCHED; read-only skips */
    reset_state();
    test_aote.remote_flag = (int8_t)0x80; test_aote.flags = AOTE_FLAG_TOUCHED;
    getinfo_status = 0x000F0001;
    ast_$purify_aote(&test_aote, 0, &status);
    ASSERT_EQ(AOTE_FLAG_TOUCHED, test_aote.flags);
    ASSERT_EQ(status_$ok, status);

    reset_state();
    test_aote.remote_flag = (int8_t)0x80; test_aote.flags = AOTE_FLAG_TOUCHED;
    test_aote.attr_flags_lo = 0x01;
    ast_$purify_aote(&test_aote, 0, &status);
    ASSERT_EQ(0, getinfo_calls);
}

TEST(purify_aote_local)
{
    status_$t status = 0x99;

    test_aote.flags = AOTE_FLAG_TOUCHED;
    test_aote.obj_type = 0x12;
    ast_$purify_aote(&test_aote, -1, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, clock_calls);
    ASSERT_EQ((uintptr_t)&test_aote.dtu_high, (uintptr_t)clock_dst);
    ASSERT_EQ(1, write_calls);
    ASSERT_EQ((char)-1, write_flags);
    ASSERT_EQ(0x12, write_first);
    ASSERT_EQ(0, test_aote.flags);                     /* TOUCHED and DIRTY off */

    reset_state();
    test_aote.flags = AOTE_FLAG_DIRTY; write_status = status_$disk_write_protected;
    ast_$purify_aote(&test_aote, 0, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, test_aote.flags);

    reset_state();
    test_aote.flags = AOTE_FLAG_DIRTY; write_status = 0x00080001;
    ast_$purify_aote(&test_aote, 0, &status);
    ASSERT_EQ(0x80080001u, (uint32_t)status);
    ASSERT_EQ(AOTE_FLAG_DIRTY, test_aote.flags);

    reset_state();
    ast_$purify_aote(&test_aote, 0, &status);          /* clean: nothing */
    ASSERT_EQ(0, write_calls); ASSERT_EQ(0, clock_calls);
}

int main(void)
{
    printf("test_process_aote (ast_$process_aote 0x00E01AD2, ast_$purify_aote 0x00E013A0)\n");

    RUN_TEST(refusals);
    RUN_TEST(drain_and_unlink);
    RUN_TEST(head_unlink_and_purify);
    RUN_TEST(wait_for_in_transition_aste);
    RUN_TEST(deactivate_failures);
    RUN_TEST(purify_aote_remote);
    RUN_TEST(purify_aote_local);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
