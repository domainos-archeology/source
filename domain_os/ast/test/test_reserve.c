/*
 * ast/test/test_reserve.c - Unit tests for AST_$RESERVE (0x00E0677E) and
 *                           AST_$SET_DTS (0x00E05540)
 *
 * Both .c files are #included directly and driven through mocks.  Pins:
 *
 *   RESERVE: the remote forward; the segment walk from the LAST page
 *   down; runs of empty entries marked in transition, handed to
 *   ast_$setup_page_read(aste, entry, page, run, 0x40) and cleared; runs
 *   broken by installed / on-disk / in-transition entries and by the end
 *   of the range; ASTE bit 14 left set; a setup failure ends the call.
 *
 *   SET_DTS: activation only with bit 0 (force TRUE); the bit-4 TOUCHED
 *   path (TRUE result, DTU stamped, remote left untouched without DIRTY);
 *   the bit 1/2/3 stores; DIRTY set in both arms.
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

#include "ast/reserve.c"
#include "ast/set_dts.c"

#define TEST_N_PAGES 32
static segmap_entry_t test_segmap[4 * TEST_N_PAGES];
segmap_entry_t *ast_segmap_base = &test_segmap[TEST_N_PAGES];
ec_$eventcount_t ast_ast_in_trans_ec;

static aote_t test_aote;
static aste_t s2, s1;               /* segments 2 and 1, seg_index 2 and 1 */
static uid_t  test_uid = { 0xAB, 0xCD };

static int inhibit_begin, inhibit_end;
void PROC1_$INHIBIT_BEGIN(void) { inhibit_begin++; }
void PROC1_$INHIBIT_END(void)   { inhibit_end++; }
static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static aote_t *lookup_result;
aote_t *ast_$lookup_aote_by_uid(uid_t *uid) { (void)uid; return lookup_result; }
static int force_calls; static int8_t force_flag; static aote_t *force_result;
aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t loc, status_$t *st, int8_t f)
{
    (void)uid; (void)loc; force_calls++; force_flag = f; *st = status_$ok; return force_result;
}
aste_t *ast_$lookup_aste(aote_t *aote, int16_t seg)
{
    (void)aote;
    if (seg == 2) return &s2;
    if (seg == 1) return &s1;
    return NULL;
}
static int create_calls;
aste_t *ast_$lookup_or_create_aste(aote_t *aote, uint16_t seg, status_$t *st)
{
    (void)aote; (void)seg; create_calls++; *st = status_$ok; return NULL;
}
static int wait_calls;
/* the transition ends: clear bit 31 everywhere (a no-op mock would spin) */
void ast_$wait_for_page_transition(void)
{
    int i;
    wait_calls++;
    for (i = 0; i < 4 * TEST_N_PAGES; i++) { test_segmap[i].entry &= ~SEGMAP_IN_TRANS; }
}
static int advance_calls;
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advance_calls++; }

#define MAX_REC 8
static int setup_calls; static aste_t *setup_aste[MAX_REC]; static uint32_t *setup_entry[MAX_REC];
static uint16_t setup_page[MAX_REC], setup_count[MAX_REC], setup_flags[MAX_REC];
static status_$t setup_status; static int setup_fail_at; static int setup_marked_ok;
void ast_$setup_page_read(aste_t *aste, uint32_t *segmap, uint16_t start_page,
                          uint16_t count, uint16_t flags, status_$t *status)
{
    int i;
    if (setup_calls < MAX_REC) {
        setup_aste[setup_calls] = aste; setup_entry[setup_calls] = segmap;
        setup_page[setup_calls] = start_page; setup_count[setup_calls] = count;
        setup_flags[setup_calls] = flags;
    }
    /* every entry of the run must be in transition on arrival */
    for (i = 0; i < count; i++) {
        if ((int32_t)segmap[i] >= 0) { setup_marked_ok = 0; }
        segmap[i] = (segmap[i] & 0xFF800000u) | (0x1000u + (uint32_t)i);
    }
    setup_calls++;
    *status = (setup_calls == setup_fail_at) ? 0x00010002 : status_$ok;
}
static int rem_calls; static uint32_t rem_start, rem_count;
void REM_FILE_$RESERVE(uid_t *vol, uid_t *uid, uint32_t start, uint32_t count, status_$t *st)
{
    (void)vol; (void)uid; rem_calls++; rem_start = start; rem_count = count; *st = 0x00060001;
}
static int clock_calls; static clock_t *clock_dst;
void TIME_$CLOCK(clock_t *c) { clock_calls++; clock_dst = c; c->high = 0xC1; c->low = 0xC2; }

static uint32_t *row(int seg) { return (uint32_t *)&test_segmap[seg * TEST_N_PAGES]; }

static void reset_state(void)
{
    memset(test_segmap, 0, sizeof(test_segmap));
    memset(&test_aote, 0, sizeof(test_aote));
    memset(&s2, 0, sizeof(s2)); memset(&s1, 0, sizeof(s1));
    s2.seg_index = 2; s2.segment = 2; s1.seg_index = 1; s1.segment = 1;
    lookup_result = &test_aote;
    force_calls = 0; force_result = NULL; force_flag = 0;
    inhibit_begin = inhibit_end = 0; lock_calls = unlock_calls = 0;
    create_calls = 0; advance_calls = 0;
    setup_calls = 0; setup_fail_at = 0; setup_marked_ok = 1;
    rem_calls = 0; clock_calls = 0; wait_calls = 0;
}

TEST(reserve_remote_forwarded)
{
    status_$t status = 0x77;

    test_aote.remote_flag = (int8_t)0x80;
    test_aote.obj_loc_net = 0x11; test_aote.obj_loc_node = 0x22;
    AST_$RESERVE(&test_uid, 40, 10, &status);

    ASSERT_EQ(1, rem_calls); ASSERT_EQ(40, rem_start); ASSERT_EQ(10, rem_count);
    ASSERT_EQ(0x00060001, status);
    ASSERT_EQ(AOTE_FLAG_BUSY, test_aote.flags);
    ASSERT_EQ(1, unlock_calls); ASSERT_EQ(1, inhibit_end);
    ASSERT_EQ(0, setup_calls);
}

TEST(reserve_two_segments_from_the_end)
{
    status_$t status = 0;
    uint32_t *r1 = row(1), *r2 = row(2);

    /* pages 60..70: segment 1 pages 28..31, segment 2 pages 0..6 */
    r1[29] = SEGMAP_VALID | 0x300;          /* installed: breaks the run */
    r2[3] = 0x00000555;                     /* on disk: breaks the run */
    r2[5] = SEGMAP_IN_TRANS;                /* ends the run at 4; waited for, then empty */

    AST_$RESERVE(&test_uid, 60, 11, &status);

    ASSERT_EQ(status_$ok, status);
    /* setup calls, segment 2 first: [0..2], [4], [5..6] after the wait,
     * then segment 1: [28], [30..31] */
    ASSERT_EQ(5, setup_calls);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ((uintptr_t)&s2, (uintptr_t)setup_aste[0]);
    ASSERT_EQ(0, setup_page[0]); ASSERT_EQ(3, setup_count[0]); ASSERT_EQ(0x40, setup_flags[0]);
    ASSERT_EQ((uintptr_t)&r2[0], (uintptr_t)setup_entry[0]);
    ASSERT_EQ(4, setup_page[1]); ASSERT_EQ(1, setup_count[1]);
    ASSERT_EQ(5, setup_page[2]); ASSERT_EQ(2, setup_count[2]);
    ASSERT_EQ((uintptr_t)&s1, (uintptr_t)setup_aste[3]);
    ASSERT_EQ(28, setup_page[3]); ASSERT_EQ(1, setup_count[3]);
    ASSERT_EQ(30, setup_page[4]); ASSERT_EQ(2, setup_count[4]);
    ASSERT_EQ(1, setup_marked_ok);
    /* transition bits cleared again */
    ASSERT_EQ(0x1000, r2[0]); ASSERT_EQ(0x1002, r2[2]); ASSERT_EQ(0x1000, r2[4]);
    ASSERT_EQ(0x1000, r2[5]); ASSERT_EQ(0x1001, r2[6]);
    ASSERT_EQ(0x1000, r1[28]); ASSERT_EQ(0x1001, r1[31]);
    ASSERT_EQ(0x00000555, r2[3]);
    /* ASTE bit 14 left set, bit 15 cleared */
    ASSERT_EQ(ASTE_FLAG_LOCKED, s2.flags); ASSERT_EQ(ASTE_FLAG_LOCKED, s1.flags);
    ASSERT_EQ(AOTE_FLAG_BUSY, test_aote.flags);
    ASSERT_EQ(3, advance_calls);            /* two ASTEs + the AOTE */
    ASSERT_EQ(1, inhibit_end);
}

TEST(reserve_range_bounds_run)
{
    status_$t status = 0;

    /* pages 33..34 only: segment 1 pages 1..2, run capped at 2 */
    AST_$RESERVE(&test_uid, 33, 2, &status);
    ASSERT_EQ(1, setup_calls);
    ASSERT_EQ(1, setup_page[0]); ASSERT_EQ(2, setup_count[0]);
    ASSERT_EQ(0, row(1)[3]);                /* untouched */
}

TEST(reserve_setup_failure_stops)
{
    status_$t status = 0;

    setup_fail_at = 1;
    AST_$RESERVE(&test_uid, 60, 11, &status);
    ASSERT_EQ(0x00010002, status);
    ASSERT_EQ(1, setup_calls);              /* segment 2 only */
    ASSERT_EQ(ASTE_FLAG_LOCKED, s2.flags);
    ASSERT_EQ(0, s1.flags);
    ASSERT_EQ(AOTE_FLAG_BUSY, test_aote.flags);
    ASSERT_EQ(2, advance_calls);
}

TEST(reserve_missing_aste)
{
    status_$t status = 0;

    AST_$RESERVE(&test_uid, 100, 1, &status);   /* segment 3: no ASTE */
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(0, setup_calls);
    ASSERT_EQ(AOTE_FLAG_BUSY, test_aote.flags);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ(1, inhibit_end);
}

TEST(reserve_zero_count_enters_body)
{
    status_$t status = 0;

    /* page_count 0: the last page (36) is BELOW the start page (37), so
     * the segment walk starts at page 5 with last_in_seg 4.  The image
     * enters the page loop before testing cur <= last_in_seg
     * (0x00E068BA bra.b 0x00e068c0), so page 5 is still handed to
     * ast_$setup_page_read; the on-disk entry at 6 bounds the run. */
    row(1)[6] = 0x00000555;
    AST_$RESERVE(&test_uid, 37, 0, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, setup_calls);
    ASSERT_EQ((uintptr_t)&s1, (uintptr_t)setup_aste[0]);
    ASSERT_EQ(5, setup_page[0]); ASSERT_EQ(1, setup_count[0]);
    ASSERT_EQ(0x1000, row(1)[5]);
    ASSERT_EQ(0x00000555, row(1)[6]);
    ASSERT_EQ(2, advance_calls);
}

TEST(set_dts_activation)
{
    uint32_t dtv[2] = { 0, 0 }, tm[2] = { 0, 0 };
    status_$t status = 0x99;
    uint8_t r;

    lookup_result = NULL;
    r = AST_$SET_DTS(0x02, &test_uid, dtv, tm, &status);
    ASSERT_EQ(0, r); ASSERT_EQ(0, force_calls); ASSERT_EQ(status_$ok, status);

    r = AST_$SET_DTS(0x03, &test_uid, dtv, tm, &status);
    ASSERT_EQ(1, force_calls); ASSERT_EQ(0xFF, (uint8_t)force_flag);
    ASSERT_EQ(0, r);
    ASSERT_EQ(2, inhibit_end);
}

TEST(set_dts_stores)
{
    clock_t dtv = { 0xD1, 0xD2 }, tm = { 0xE1, 0xE2 };
    status_$t status = 0;
    uint8_t r;

    r = AST_$SET_DTS(0x0E, &test_uid, (uint32_t *)&dtv, (uint32_t *)&tm, &status);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0xD1, test_aote.dtv_high); ASSERT_EQ(0xD2, test_aote.dtv_low);
    ASSERT_EQ(0xE1, test_aote.dtm_high); ASSERT_EQ(0xE2, test_aote.dtm_low);
    ASSERT_EQ(0xE1, test_aote.dta_high); ASSERT_EQ(0xE2, test_aote.dta_low);
    ASSERT_EQ(0xE1, test_aote.dtu_high); ASSERT_EQ(0xE2, test_aote.dtu_low);
    ASSERT_EQ(AOTE_FLAG_DIRTY, test_aote.flags);
    ASSERT_EQ(2, lock_calls); ASSERT_EQ(2, unlock_calls);

    reset_state();
    r = AST_$SET_DTS(0x02, &test_uid, (uint32_t *)&dtv, (uint32_t *)&tm, &status);
    ASSERT_EQ(0xD1, test_aote.dtv_high);
    ASSERT_EQ(0, test_aote.dtm_high); ASSERT_EQ(0, test_aote.dtu_high);
}

TEST(set_dts_touched_path)
{
    uint32_t dtv[2] = { 0, 0 }, tm[2] = { 0, 0 };
    status_$t status = 0;
    uint8_t r;

    test_aote.flags = AOTE_FLAG_TOUCHED;
    r = AST_$SET_DTS(0x1E, &test_uid, dtv, tm, &status);
    ASSERT_EQ(0xFF, r);
    ASSERT_EQ(1, clock_calls);
    ASSERT_EQ((uintptr_t)&test_aote.dtu_high, (uintptr_t)clock_dst);
    ASSERT_EQ(AOTE_FLAG_DIRTY, test_aote.flags);
    ASSERT_EQ(0, test_aote.dtv_high);                  /* bit 1 store skipped */

    /* remote: TOUCHED cleared, TRUE, nothing else */
    reset_state();
    test_aote.flags = AOTE_FLAG_TOUCHED; test_aote.remote_flag = (int8_t)0x80;
    r = AST_$SET_DTS(0x10, &test_uid, dtv, tm, &status);
    ASSERT_EQ(0xFF, r);
    ASSERT_EQ(0, test_aote.flags);
    ASSERT_EQ(0, clock_calls);
    ASSERT_EQ(1, lock_calls); ASSERT_EQ(1, unlock_calls);

    /* bit 4 without TOUCHED takes the store arm */
    reset_state();
    r = AST_$SET_DTS(0x10, &test_uid, dtv, tm, &status);
    ASSERT_EQ(0, r);
    ASSERT_EQ(AOTE_FLAG_DIRTY, test_aote.flags);
}

int main(void)
{
    printf("test_reserve (AST_$RESERVE 0x00E0677E, AST_$SET_DTS 0x00E05540)\n");

    RUN_TEST(reserve_remote_forwarded);
    RUN_TEST(reserve_two_segments_from_the_end);
    RUN_TEST(reserve_range_bounds_run);
    RUN_TEST(reserve_setup_failure_stops);
    RUN_TEST(reserve_missing_aste);
    RUN_TEST(reserve_zero_count_enters_body);
    RUN_TEST(set_dts_activation);
    RUN_TEST(set_dts_stores);
    RUN_TEST(set_dts_touched_path);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
