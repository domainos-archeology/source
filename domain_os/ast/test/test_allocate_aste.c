/*
 * ast/test/test_allocate_aste.c - Unit tests for AST_$ALLOCATE_ASTE
 *                                 (0x00E01F1C)
 *
 * The test #includes ast/allocate_aste.c directly and drives the real
 * routine through a mocked AST_$DEACTIVATE_SEGMENT.  It pins the points
 * the re-emission fixed:
 *
 *   - the second-chance bit is flags 0x4000 (`bclr.b #0x6,(0x12,A2)` acts
 *     on the HIGH byte of the flags word), not 0x0040;
 *   - the scan visits twelve entries (moveq #0xb / dbf);
 *   - candidates are retried best-first (A6-0x8 before A6-0x4), and a
 *     candidate success does not move AST_$ASTE_SCAN_POS;
 *   - the AREA / remote / local counters and AST_$ALLOC_TOTAL_AST.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Avoid the macOS uid_t conflict - must come AFTER the system includes. */
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

#include "ast/allocate_aste.c"

/* ==========================================================================
 * Host storage for the AST_ cells the routine touches
 * ========================================================================== */

#define TEST_N_ASTES 16

/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);


/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

#define MAX_DEACT 64
static int     deact_calls;
static aste_t *deact_astes[MAX_DEACT];
/* Which ASTEs the mock is willing to deactivate (index into AST_$AOT.aste). */
static int     deact_ok[TEST_N_ASTES];

void AST_$DEACTIVATE_SEGMENT(aste_t *aste, int8_t purge, int8_t keep,
                             status_$t *status)
{
    (void)purge; (void)keep;
    if (deact_calls < MAX_DEACT) { deact_astes[deact_calls] = aste; }
    deact_calls++;
    *status = deact_ok[aste - AST_$AOT.aste] ? status_$ok
                                          : status_$ast_segment_not_deactivatable;
}

static int crash_calls;
void CRASH_SYSTEM(const status_$t *status_p)
{
    (void)status_p;
    crash_calls++;
}

static void reset_state(void)
{
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    AST_$FREE_ASTE_HEAD = NULL;
    AST_$ASTE_SCAN_POS = &AST_$AOT.aste[0];
    AST_$ASTE_LIMIT = &AST_$AOT.aste[TEST_N_ASTES];
    AST_$SIZE_AST = TEST_N_ASTES;
    AST_$ALLOC_WORST_AST = 0;
    AST_$ALLOC_TOTAL_AST = 0;
    AST_$FREE_ASTES = 0;
    AST_$ASTE_AREA_CNT = 10;
    AST_$ASTE_R_CNT = 10;
    AST_$ASTE_L_CNT = 10;
    deact_calls = 0;
    memset(deact_astes, 0, sizeof(deact_astes));
    memset(deact_ok, 0, sizeof(deact_ok));
    crash_calls = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E01F2A..0x00E01F3C: free list pop, no counters but the total */
TEST(free_list_pop)
{
    aste_t *r;

    AST_$AOT.aste[3].next = &AST_$AOT.aste[7];
    AST_$AOT.aste[3].flags = ASTE_FLAG_AREA;
    AST_$FREE_ASTE_HEAD = &AST_$AOT.aste[3];
    AST_$FREE_ASTES = 2;

    r = AST_$ALLOCATE_ASTE();

    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[3], (uintptr_t)r);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[7], (uintptr_t)AST_$FREE_ASTE_HEAD);
    ASSERT_EQ(1, AST_$FREE_ASTES);
    ASSERT_EQ(1, AST_$ALLOC_TOTAL_AST);
    ASSERT_EQ(10, AST_$ASTE_AREA_CNT);      /* no counter change on this path */
    ASSERT_EQ(0, deact_calls);
}

/* An empty entry right after the scan position is taken at once. */
TEST(scan_takes_first_empty_entry)
{
    aste_t *r;

    /* Entries 1..: 1 is wired, 2 is in transition, 3 is empty and remote */
    AST_$AOT.aste[1].wire_count = 1;
    AST_$AOT.aste[2].flags = ASTE_FLAG_IN_TRANS;
    AST_$AOT.aste[3].flags = ASTE_FLAG_REMOTE;
    deact_ok[3] = 1;

    r = AST_$ALLOCATE_ASTE();

    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[3], (uintptr_t)r);
    ASSERT_EQ(1, deact_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[3], (uintptr_t)AST_$ASTE_SCAN_POS);
    ASSERT_EQ(9, AST_$ASTE_R_CNT);
    ASSERT_EQ(10, AST_$ASTE_L_CNT);
    ASSERT_EQ(1, AST_$ALLOC_TOTAL_AST);
}

/* bit 14 is cleared as a second chance and the entry skipped */
TEST(second_chance_clears_bit_14)
{
    aste_t *r;

    AST_$AOT.aste[1].flags = ASTE_FLAG_LOCKED | ASTE_FLAG_BUSY;
    deact_ok[1] = 1;
    deact_ok[2] = 1;

    r = AST_$ALLOCATE_ASTE();

    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[2], (uintptr_t)r);
    /* 0x4000 cleared, 0x0040 untouched */
    ASSERT_EQ(ASTE_FLAG_BUSY, AST_$AOT.aste[1].flags);
    ASSERT_EQ(1, deact_calls);
}

/* Twelve entries are scanned; the thirteenth is not */
TEST(scan_visits_twelve_entries)
{
    aste_t *r;
    int i;

    /* every entry holds pages, so none is deactivated during the scan */
    for (i = 0; i < TEST_N_ASTES; i++) {
        AST_$AOT.aste[i].page_count = 5;
    }
    /* only entry 13 would succeed - out of the scan's reach */
    deact_ok[13] = 1;
    /* entries 1..12 hold pages; the best candidate is the smallest count */
    AST_$AOT.aste[6].page_count = 2;
    AST_$AOT.aste[9].page_count = 3;

    r = AST_$ALLOCATE_ASTE();

    /* scan: none taken; candidates 6 then 9 both refused; sweep from the
     * scan position (entry 12) reaches 13 */
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[13], (uintptr_t)r);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[6], (uintptr_t)deact_astes[0]);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[9], (uintptr_t)deact_astes[1]);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[13], (uintptr_t)deact_astes[2]);
    ASSERT_EQ(3, deact_calls);
    ASSERT_EQ(1, AST_$ALLOC_WORST_AST);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[13], (uintptr_t)AST_$ASTE_SCAN_POS);
}

/* Candidate order is best (fewest pages) first, and a candidate success
 * leaves the scan position where the scan ended. */
TEST(candidates_best_first_no_scan_pos_move)
{
    aste_t *r;
    int i;

    for (i = 0; i < TEST_N_ASTES; i++) {
        AST_$AOT.aste[i].page_count = 5;
    }
    AST_$AOT.aste[4].page_count = 1;      /* best */
    AST_$AOT.aste[2].page_count = 3;      /* second */
    deact_ok[4] = 0;
    deact_ok[2] = 1;

    r = AST_$ALLOCATE_ASTE();

    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[2], (uintptr_t)r);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[4], (uintptr_t)deact_astes[0]);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[2], (uintptr_t)deact_astes[1]);
    ASSERT_EQ(2, deact_calls);
    /* scan position stays at the twelfth entry visited */
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[12], (uintptr_t)AST_$ASTE_SCAN_POS);
    ASSERT_EQ(0, AST_$ALLOC_WORST_AST);
    ASSERT_EQ(9, AST_$ASTE_L_CNT);
}

/* The scan wraps at AST_$ASTE_LIMIT back to AST_$AOT.aste[0] */
TEST(scan_wraps_at_limit)
{
    aste_t *r;

    AST_$ASTE_SCAN_POS = &AST_$AOT.aste[TEST_N_ASTES - 1];
    deact_ok[0] = 1;

    r = AST_$ALLOCATE_ASTE();

    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[0], (uintptr_t)r);
    ASSERT_EQ(1, deact_calls);
}

/* The last-resort sweep only honours bit 14: it tries to deactivate a
 * wired entry the scan had rejected, and counts its success as "worst". */
TEST(sweep_ignores_wire_count)
{
    aste_t *r;
    int i;

    for (i = 0; i < TEST_N_ASTES; i++) {
        AST_$AOT.aste[i].wire_count = 1;      /* the scan rejects every entry */
    }
    deact_ok[14] = 1;                      /* beyond the scan's reach */

    r = AST_$ALLOCATE_ASTE();

    /* scan: 1..12, no calls; sweep from 12: 13 refused, 14 taken */
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[14], (uintptr_t)r);
    ASSERT_EQ(2, deact_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[13], (uintptr_t)deact_astes[0]);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[14], (uintptr_t)deact_astes[1]);
    ASSERT_EQ(1, AST_$ALLOC_WORST_AST);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aste[14], (uintptr_t)AST_$ASTE_SCAN_POS);
    ASSERT_EQ(0, crash_calls);
}

int main(void)
{
    printf("test_allocate_aste (AST_$ALLOCATE_ASTE 0x00E01F1C)\n");

    RUN_TEST(free_list_pop);
    RUN_TEST(scan_takes_first_empty_entry);
    RUN_TEST(second_chance_clears_bit_14);
    RUN_TEST(scan_visits_twelve_entries);
    RUN_TEST(candidates_best_first_no_scan_pos_move);
    RUN_TEST(scan_wraps_at_limit);
    RUN_TEST(sweep_ignores_wire_count);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
