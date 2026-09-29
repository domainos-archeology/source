/*
 * ast/test/test_dismount.c - Unit tests for AST_$DISMOUNT (0x00E069CA)
 *
 * The test #includes ast/dismount.c directly and drives the real routine
 * through mocked callees.  It pins:
 *
 *   - the volume bit is set in AST_$DATA.vol_info_count on entry and cleared
 *     on every exit, and AST_$DISM_SEQN goes up by one;
 *   - the activation-count wait: EC_$WAIT gets { &AST_$DISM_EC, 0, 0 } and
 *     { value + 1, 0, 0 }, bracketed by an unlock/lock;
 *   - the AOT walk stops at AST_$AOTE_LIMIT and skips remote AOTEs, other
 *     volumes, unused entries (first UID byte zero) and the paging file;
 *   - a matching AOTE is processed with (purge = flags, keep = TRUE,
 *     wait = TRUE) and released; an in-transition one is waited for and
 *     re-examined;
 *   - a processing failure records AST_$DISMOUNT_FAILED_PTR, skips
 *     VTOC_$DISMOUNT and returns that status.
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

#include "ast/dismount.c"

/* AST_ block cells (ast_data.c is not included) */
#define TEST_N_AOTES 6
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
uid_t     NETWORK_$PAGING_FILE_UID = { 0x99990000, 0x00009999 };

static int inhibit_begin_calls, inhibit_end_calls;
void PROC1_$INHIBIT_BEGIN(void) { inhibit_begin_calls++; }
void PROC1_$INHIBIT_END(void)   { inhibit_end_calls++; }

static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static int wait_calls;
static ec_$wait_ecs_t wait_ecs;
static ec_$wait_vals_t wait_vals;
static uint16_t wait_vol;
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    wait_calls++;
    wait_ecs = ecs;
    wait_vals = vals;
    /* the activation on the volume finishes */
    AST_$DATA.vol_indices[wait_vol]--;
    return 0;
}

static int intrans_calls;
static aote_t *intrans_aote;
void AST_$WAIT_FOR_AST_INTRANS(void)
{
    intrans_calls++;
    if (intrans_aote != NULL) {
        intrans_aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    }
}

#define MAX_PROC 8
static int       process_calls;
static aote_t   *process_aotes[MAX_PROC];
static boolean   process_f1, process_f2, process_f3;
static aote_t   *process_fail_aote;
static status_$t process_fail_status;
uint16_t ast_$process_aote(aote_t *aote, boolean flags1, boolean flags2,
                           boolean flags3, status_$t *status)
{
    if (process_calls < MAX_PROC) { process_aotes[process_calls] = aote; }
    process_calls++;
    process_f1 = flags1; process_f2 = flags2; process_f3 = flags3;
    *status = (aote == process_fail_aote) ? process_fail_status : status_$ok;
    return 0;
}

static int     release_calls;
static aote_t *release_aotes[MAX_PROC];
void ast_$release_aote(aote_t *aote)
{
    if (release_calls < MAX_PROC) { release_aotes[release_calls] = aote; }
    release_calls++;
}

static int       vtoc_calls;
static uint16_t  vtoc_vol;
static uint8_t   vtoc_flags;
static status_$t vtoc_status;
void VTOC_$DISMOUNT(uint16_t vol_idx, uint8_t flags, status_$t *status)
{
    vtoc_calls++;
    vtoc_vol = vol_idx;
    vtoc_flags = flags;
    *status = vtoc_status;
}

static void set_aote(int i, int8_t remote, uint8_t vol, uint32_t uid_high,
                     uint32_t uid_low)
{
    AST_$AOT.aote[i].remote_flag = remote;
    AST_$AOT.aote[i].vol_index = vol;
    AST_$AOT.aote[i].uid.high = uid_high;
    AST_$AOT.aote[i].uid.low = uid_low;
}

static void reset_state(void)
{
    memset(AST_$AOT.aote, 0, sizeof(AST_$AOT.aote));
    AST_$AOTE_LIMIT = &AST_$AOT.aote[TEST_N_AOTES];
    AST_$DISM_SEQN = 40;
    memset(&AST_$DISM_EC, 0, sizeof(AST_$DISM_EC));
    AST_$DISM_EC.value = 7;
    memset(AST_$DATA.vol_indices, 0, sizeof(AST_$DATA.vol_indices));
    AST_$DATA.vol_info_count = 0;
    AST_$DISMOUNT_FAILED_PTR = NULL;
    inhibit_begin_calls = inhibit_end_calls = 0;
    lock_calls = unlock_calls = 0;
    wait_calls = 0; wait_vol = 0;
    intrans_calls = 0; intrans_aote = NULL;
    process_calls = 0; memset(process_aotes, 0, sizeof(process_aotes));
    process_fail_aote = NULL; process_fail_status = status_$ok;
    release_calls = 0; memset(release_aotes, 0, sizeof(release_aotes));
    vtoc_calls = 0; vtoc_status = status_$ok;
}

TEST(walk_selects_only_local_entries_on_volume)
{
    status_$t status = 0x1234;

    set_aote(0, (int8_t)0x80, 2, 0x01000000, 1);         /* remote */
    set_aote(1, 0, 3, 0x01000000, 1);                     /* other volume */
    set_aote(2, 0, 2, 0x00FFFFFF, 1);                     /* unused: first byte 0 */
    set_aote(3, 0, 2, 0x99990000, 0x00009999);            /* paging file */
    set_aote(4, 0, 2, 0x01000000, 5);                     /* ours */
    set_aote(5, 0, 2, 0x02000000, 6);                     /* ours */

    AST_$DISMOUNT(2, 0xFF, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(2, process_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aote[4], (uintptr_t)process_aotes[0]);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aote[5], (uintptr_t)process_aotes[1]);
    ASSERT_EQ(0xFF, (uint8_t)process_f1);
    ASSERT_EQ(0xFF, (uint8_t)process_f2);
    ASSERT_EQ(0xFF, (uint8_t)process_f3);
    ASSERT_EQ(2, release_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aote[4], (uintptr_t)release_aotes[0]);
    ASSERT_EQ(1, vtoc_calls);
    ASSERT_EQ(2, vtoc_vol);
    ASSERT_EQ(0xFF, vtoc_flags);
    ASSERT_EQ(41, AST_$DISM_SEQN);
    ASSERT_EQ(0, AST_$DATA.vol_info_count);           /* bit 2 set, then cleared */
    ASSERT_EQ(1, inhibit_begin_calls);
    ASSERT_EQ(1, inhibit_end_calls);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, wait_calls);
}

TEST(waits_for_activations_on_volume)
{
    status_$t status = 0;

    AST_$DATA.vol_indices[3] = 2;
    wait_vol = 3;

    AST_$DISMOUNT(3, 0, &status);

    ASSERT_EQ(2, wait_calls);
    ASSERT_EQ((uintptr_t)&AST_$DISM_EC, (uintptr_t)wait_ecs.ec[0]);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)wait_ecs.ec[1]);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)wait_ecs.ec[2]);
    ASSERT_EQ(8, wait_vals.val[0]);              /* value + 1 */
    ASSERT_EQ(0, wait_vals.val[1]);
    ASSERT_EQ(0, wait_vals.val[2]);
    /* one unlock/lock pair per wait, plus the final unlock */
    ASSERT_EQ(3, lock_calls);
    ASSERT_EQ(3, unlock_calls);
    ASSERT_EQ(1, vtoc_calls);
}

TEST(in_transition_entry_is_retested)
{
    status_$t status = 0;

    set_aote(1, 0, 1, 0x01000000, 1);
    AST_$AOT.aote[1].flags = AOTE_FLAG_IN_TRANS;
    intrans_aote = &AST_$AOT.aote[1];

    AST_$DISMOUNT(1, 0, &status);

    ASSERT_EQ(1, intrans_calls);
    ASSERT_EQ(1, process_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aote[1], (uintptr_t)process_aotes[0]);
    ASSERT_EQ(0, (uint8_t)process_f1);           /* purge = flags = 0 */
}

TEST(processing_failure_records_aote_and_skips_vtoc)
{
    status_$t status = 0;

    set_aote(0, 0, 4, 0x01000000, 1);
    set_aote(1, 0, 4, 0x01000000, 2);
    set_aote(2, 0, 4, 0x01000000, 3);
    process_fail_aote = &AST_$AOT.aote[1];
    process_fail_status = status_$ast_segment_not_deactivatable;

    AST_$DISMOUNT(4, 0, &status);

    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);
    ASSERT_EQ(2, process_calls);                 /* stops at the failure */
    ASSERT_EQ(1, release_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aote[1], (uintptr_t)AST_$DISMOUNT_FAILED_PTR);
    ASSERT_EQ(0, vtoc_calls);
    ASSERT_EQ(0, AST_$DATA.vol_info_count);           /* still cleared */
    ASSERT_EQ(1, inhibit_end_calls);
    ASSERT_EQ(1, unlock_calls);
}

TEST(vtoc_status_is_returned)
{
    status_$t status = 0;

    vtoc_status = 0x00020001;
    AST_$DISMOUNT(5, 1, &status);

    ASSERT_EQ(0x00020001, status);
    ASSERT_EQ(1, vtoc_flags);
}

int main(void)
{
    printf("test_dismount (AST_$DISMOUNT 0x00E069CA)\n");

    RUN_TEST(walk_selects_only_local_entries_on_volume);
    RUN_TEST(waits_for_activations_on_volume);
    RUN_TEST(in_transition_entry_is_retested);
    RUN_TEST(processing_failure_records_aote_and_skips_vtoc);
    RUN_TEST(vtoc_status_is_returned);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
