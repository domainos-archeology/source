/*
 * ast/test/test_purify.c - Unit tests for AST_$PURIFY (0x00E0567A)
 *
 * The test #includes ast/purify.c directly and drives the real routine
 * through mocks.  It pins:
 *
 *   - flag bits 5..14 are refused with "incompatible request";
 *   - the local, bit-1-clear pass moves PFT MODIFIED into the MMAPE and
 *     stamps DTM/DTA/DTV before purify_aote;
 *   - the flush pass (bit 1): PMAP_$FLUSH over all 32 pages with flag 4
 *     from bit 15, 0x3000C tolerated, ast_$update_aste, then
 *     DBUF_$UPDATE_VOL for a local object;
 *   - bit 0 selects one segment; bit 4 walks the page list, coalescing
 *     consecutive descending pages of one segment into one FLUSH;
 *   - the remote chunk loop with REM_FILE_$PURIFY and the final call;
 *   - bit 3 turns a clean status into 0x3EFFF when pages were written;
 *   - an inactive object is activated only with bit 1.
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

#include "ast/purify.c"

#define TEST_N_PAGES 32
#define TEST_N_FRAMES 0x400
/* The AST_ module blocks (ast/ast.h) and the segment map (pmap/pmap.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
static mmape_t        test_mmapes[TEST_N_FRAMES];
static uint32_t       test_pft[TEST_N_FRAMES];
mmape_t        *mmap_mmape_base = test_mmapes;
uint32_t       *mmu_pft_base    = test_pft;

static aote_t test_aote;
static aste_t s1, s2;                   /* segments 2 and 1 */
static uid_t  test_uid = { 0x1234, 0x5678 };

static int inhibit_begin, inhibit_end;
void PROC1_$INHIBIT_BEGIN(void) { inhibit_begin++; }
void PROC1_$INHIBIT_END(void)   { inhibit_end++; }
static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static aote_t *lookup_result;
aote_t *ast_$lookup_aote_by_uid(uid_t *uid) { (void)uid; return lookup_result; }
static int force_calls; static aote_t *force_result;
aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t loc, status_$t *st, int8_t f)
{
    (void)uid; (void)loc; (void)f; force_calls++; *st = status_$ok; return force_result;
}
static int wait_calls;
void AST_$WAIT_FOR_AST_INTRANS(void) { wait_calls++; }
void ast_$wait_for_page_transition(void) { }
static int advance_calls;
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advance_calls++; }

#define MAX_REC 8
static int flush_calls; static aste_t *flush_aste[MAX_REC]; static uint16_t flush_start[MAX_REC];
static int16_t flush_count[MAX_REC]; static uint16_t flush_flags[MAX_REC];
static int16_t flush_result; static status_$t flush_status;
int16_t PMAP_$FLUSH(struct aste_t *aste, uint32_t *segmap, uint16_t start_page,
                    int16_t count, uint16_t flags, status_$t *status)
{
    (void)segmap;
    if (flush_calls < MAX_REC) {
        flush_aste[flush_calls] = aste; flush_start[flush_calls] = start_page;
        flush_count[flush_calls] = count; flush_flags[flush_calls] = flags;
    }
    flush_calls++;
    *status = flush_status;
    return flush_result;
}
static int update_calls; static status_$t update_status;
void ast_$update_aste(aste_t *aste, segmap_entry_t *segmap, boolean flags, status_$t *status)
{
    (void)aste; (void)segmap; (void)flags; update_calls++; *status = update_status;
}
static int clock_calls, abs_clock_calls;
void TIME_$CLOCK(clock_t *c)     { clock_calls++; c->high = 0x1111; c->low = 0x22; }
void TIME_$ABS_CLOCK(clock_t *c) { abs_clock_calls++; c->high = 0x3333; c->low = 0x44; }
static int purify_aote_calls;
void ast_$purify_aote(aote_t *aote, boolean flags, status_$t *status) { (void)aote; (void)flags; purify_aote_calls++; *status = status_$ok; }
static int dbuf_calls; static uint16_t dbuf_vol;
void DBUF_$UPDATE_VOL(uint16_t vol_idx, void *uid_p) { (void)uid_p; dbuf_calls++; dbuf_vol = vol_idx; }
static int rem_calls; static uint16_t rem_flags[MAX_REC]; static int16_t rem_page[MAX_REC];
static status_$t rem_statuses[MAX_REC];
void REM_FILE_$PURIFY(uid_t *vol_uid, uid_t *file_uid, uint16_t *flags, int16_t page_index, status_$t *status)
{
    (void)vol_uid; (void)file_uid;
    if (rem_calls < MAX_REC) { rem_flags[rem_calls] = *flags; rem_page[rem_calls] = page_index; }
    *status = (rem_calls < MAX_REC) ? rem_statuses[rem_calls] : status_$ok;
    rem_calls++;
}

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(test_mmapes, 0, sizeof(test_mmapes));
    memset(test_pft, 0, sizeof(test_pft));
    memset(&test_aote, 0, sizeof(test_aote));
    memset(&s1, 0, sizeof(s1)); memset(&s2, 0, sizeof(s2));
    s1.segment = 2; s1.seg_index = 2; s1.aote = &test_aote; s1.next = &s2; s1.page_count = 1;
    s2.segment = 1; s2.seg_index = 1; s2.aote = &test_aote; s2.page_count = 1;
    test_aote.aste_list = &s1;
    test_aote.vol_index = 3;
    test_aote.length = 0x28000;
    lookup_result = &test_aote;
    force_calls = 0; force_result = NULL;
    inhibit_begin = inhibit_end = 0; lock_calls = unlock_calls = 0;
    wait_calls = 0; advance_calls = 0;
    flush_calls = 0; flush_result = 5; flush_status = status_$ok;
    update_calls = 0; update_status = status_$ok;
    clock_calls = abs_clock_calls = 0; purify_aote_calls = 0;
    dbuf_calls = 0; rem_calls = 0;
    memset(rem_statuses, 0, sizeof(rem_statuses));
}

TEST(bad_flags_refused)
{
    status_$t status = 0;
    uint16_t r = AST_$PURIFY(&test_uid, 0x0020, 0, NULL, 0, &status);
    ASSERT_EQ(status_$ast_incompatible_request, status);
    ASSERT_EQ(0x0020, r);
    ASSERT_EQ(0, inhibit_begin);
}

TEST(local_mark_modified_pass)
{
    status_$t status = 0x77;
    uint32_t *row2 = (uint32_t *)PMAP_SEGMAP_ROW(2);

    row2[4] = SEGMAP_VALID | 0x210;
    test_pft[0x210] = PFT_FLAG_MODIFIED | 0x0001;
    row2[5] = SEGMAP_VALID | 0x211;             /* clean */

    AST_$PURIFY(&test_uid, 0, 0, NULL, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, flush_calls);
    ASSERT_EQ(0x0001, test_pft[0x210]);
    ASSERT_EQ(MMAPE_FLAG2_MODIFIED, test_mmapes[0x210].flags2);
    ASSERT_EQ(0, test_mmapes[0x211].flags2);
    ASSERT_EQ(1, clock_calls); ASSERT_EQ(1, abs_clock_calls);
    ASSERT_EQ(0x1111, test_aote.dtm_high); ASSERT_EQ(0x1111, test_aote.dta_high);
    ASSERT_EQ(0x3333, test_aote.dtv_high);
    ASSERT_EQ(AOTE_FLAG_BUSY | AOTE_FLAG_DIRTY, test_aote.flags);
    ASSERT_EQ(1, purify_aote_calls);
    ASSERT_EQ(0, s1.flags); ASSERT_EQ(0, s2.flags);
    ASSERT_EQ(3, advance_calls);                /* two ASTEs + the AOTE */
    ASSERT_EQ(0, dbuf_calls);
    ASSERT_EQ(1, inhibit_end);
}

TEST(flush_pass_local)
{
    status_$t status = 0;

    AST_$PURIFY(&test_uid, 0x8002, 0, NULL, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(2, flush_calls);
    ASSERT_EQ((uintptr_t)&s1, (uintptr_t)flush_aste[0]);
    ASSERT_EQ(0, flush_start[0]); ASSERT_EQ(0x20, flush_count[0]); ASSERT_EQ(4, flush_flags[0]);
    ASSERT_EQ(2, update_calls);
    ASSERT_EQ(0, clock_calls);
    ASSERT_EQ(1, purify_aote_calls);
    ASSERT_EQ(1, dbuf_calls); ASSERT_EQ(3, dbuf_vol);
}

TEST(one_segment_and_page_list)
{
    status_$t status = 0;
    uint32_t list[4];

    AST_$PURIFY(&test_uid, 0x0003, 1, NULL, 0, &status);
    ASSERT_EQ(1, flush_calls);
    ASSERT_EQ((uintptr_t)&s2, (uintptr_t)flush_aste[0]);
    ASSERT_EQ(0, flush_flags[0]);

    /* pages 2:7, 2:6, 2:5 (one run of three from page 5), then 1:3 */
    reset_state();
    list[0] = (2 << 5) | 7; list[1] = (2 << 5) | 6; list[2] = (2 << 5) | 5; list[3] = (1 << 5) | 3;
    AST_$PURIFY(&test_uid, 0x0012, 0, list, 4, &status);
    ASSERT_EQ(2, flush_calls);
    ASSERT_EQ((uintptr_t)&s1, (uintptr_t)flush_aste[0]);
    ASSERT_EQ(5, flush_start[0]); ASSERT_EQ(3, flush_count[0]);
    ASSERT_EQ((uintptr_t)&s2, (uintptr_t)flush_aste[1]);
    ASSERT_EQ(3, flush_start[1]); ASSERT_EQ(1, flush_count[1]);
}

TEST(flush_0x3000c_tolerated_other_fails)
{
    status_$t status = 0;

    flush_status = 0x8003000C;
    AST_$PURIFY(&test_uid, 0x0002, 0, NULL, 0, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(2, update_calls);

    reset_state();
    flush_status = 0x00050007;
    AST_$PURIFY(&test_uid, 0x0002, 0, NULL, 0, &status);
    ASSERT_EQ(0x00050007, status);
    ASSERT_EQ(1, flush_calls);
    ASSERT_EQ(0, s1.flags);                      /* bit 15 cleared, no advance for it */
    ASSERT_EQ(1, advance_calls);                 /* only the AOTE's */
    ASSERT_EQ(0, purify_aote_calls);
    ASSERT_EQ(0, dbuf_calls);
}

TEST(bit3_stops_and_reports_more)
{
    status_$t status = 0;

    flush_result = 0x40;
    AST_$PURIFY(&test_uid, 0x000A, 0, NULL, 0, &status);
    ASSERT_EQ(1, flush_calls);                   /* stopped after 0x40 pages */
    ASSERT_EQ(0x3EFFF, status);
}

TEST(remote_chunk_loop)
{
    status_$t status = 0;

    test_aote.remote_flag = (int8_t)0x80;
    test_aote.length = 0x28000;                  /* 3 chunks */
    rem_statuses[0] = 0x3EFFF; rem_statuses[1] = 0x3EFFF; rem_statuses[2] = 0x3EFFF;
    rem_statuses[3] = status_$ok;

    AST_$PURIFY(&test_uid, 0x0002, 9, NULL, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(4, rem_calls);
    ASSERT_EQ(0x000A, rem_flags[0]); ASSERT_EQ(0x000A, rem_flags[2]);
    ASSERT_EQ(0x0002, rem_flags[3]);             /* the final un-flagged call */
    ASSERT_EQ(9, rem_page[0]);
    ASSERT_EQ(0, dbuf_calls);

    /* bit 2 suppresses it; bit 0 makes a single call */
    reset_state();
    test_aote.remote_flag = (int8_t)0x80;
    AST_$PURIFY(&test_uid, 0x0006, 0, NULL, 0, &status);
    ASSERT_EQ(0, rem_calls);
    reset_state();
    test_aote.remote_flag = (int8_t)0x80;
    AST_$PURIFY(&test_uid, 0x8003, 2, NULL, 0, &status);
    ASSERT_EQ(1, rem_calls); ASSERT_EQ(0x0003, rem_flags[0]);
}

TEST(inactive_object)
{
    status_$t status = 0;

    lookup_result = NULL;
    AST_$PURIFY(&test_uid, 0, 0, NULL, 0, &status);
    ASSERT_EQ(0, force_calls);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, unlock_calls); ASSERT_EQ(1, inhibit_end);

    reset_state();
    lookup_result = NULL; force_result = &test_aote;
    AST_$PURIFY(&test_uid, 2, 0, NULL, 0, &status);
    ASSERT_EQ(1, force_calls);
    ASSERT_EQ(0, flush_calls);
    ASSERT_EQ(1, dbuf_calls); ASSERT_EQ(3, dbuf_vol);
}

int main(void)
{
    printf("test_purify (AST_$PURIFY 0x00E0567A)\n");

    RUN_TEST(bad_flags_refused);
    RUN_TEST(local_mark_modified_pass);
    RUN_TEST(flush_pass_local);
    RUN_TEST(one_segment_and_page_list);
    RUN_TEST(flush_0x3000c_tolerated_other_fails);
    RUN_TEST(bit3_stops_and_reports_more);
    RUN_TEST(remote_chunk_loop);
    RUN_TEST(inactive_object);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
