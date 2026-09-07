/*
 * Tests for ast_$count_valid_pages (0x00E0305C).
 *
 * The test #includes the real ast/count_valid_pages.c and drives the real
 * function through mocked dependencies.  It covers what this pass changed:
 *
 *   - the first stack argument is a pointer INTO the segment map, not an
 *     ASTE: `movea.l (0x8,A6),A3` at 0x00E03064 is handed straight to
 *     ast_$clear_transition_bits with `pea (A3)` at 0x00E0307A;
 *   - the read-only test is bit 1 of AST_$TOUCH's own flags word
 *     (`btst.b #0x1,(0x1d,A2)` at 0x00E0306E), reached through the static
 *     link, not a byte of the AOTE;
 *   - ast_$allocate_pages is called as (count, 1, ppn_array) -- three
 *     stack slots, `move.w D2w` last, so the COUNT is the first argument
 *     (0x00E03090-0x00E0309A);
 *   - the zero loop walks the array with `move.l (A3)+` at 0x00E030A8,
 *     i.e. ASCENDING, not from the top down;
 *   - the read-only path returns the ORIGINAL count (D2 is untouched),
 *     the normal path returns what was allocated.
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "ast/ast_internal.h"

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

#define MAX_PAGES 16

static int n_clear_trans;
static uint32_t *last_clear_segmap;
static uint16_t last_clear_count;

static int n_alloc;
static int16_t last_alloc_count;
static int16_t last_alloc_min;
static uint32_t *last_alloc_array;
static int16_t alloc_result;

static int n_zero;
static uint32_t zero_order[MAX_PAGES];

void ast_$clear_transition_bits(uint32_t *segmap, uint16_t count)
{
    n_clear_trans++;
    last_clear_segmap = segmap;
    last_clear_count = count;
}

int16_t ast_$allocate_pages(int16_t count, int16_t min_count,
                            uint32_t *ppn_array)
{
    n_alloc++;
    last_alloc_count = count;
    last_alloc_min = min_count;
    last_alloc_array = ppn_array;
    return alloc_result;
}

void ZERO_PAGE(uint32_t ppn)
{
    if (n_zero < MAX_PAGES) {
        zero_order[n_zero] = ppn;
    }
    n_zero++;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "ast/count_valid_pages.c"

/* ------------------------------------------------------------------ */

static int tests_failed;

#define CHECK(cond) do {                                                \
    if (!(cond)) {                                                      \
        printf("FAILED at line %d: %s\n", __LINE__, #cond);             \
        tests_failed++;                                                 \
    }                                                                   \
} while (0)

static uint32_t segmap[8];
static uint32_t ppn_array[MAX_PAGES];

static void reset_all(void)
{
    n_clear_trans = 0;
    last_clear_segmap = NULL;
    last_clear_count = 0;
    n_alloc = 0;
    last_alloc_count = -1;
    last_alloc_min = -1;
    last_alloc_array = NULL;
    alloc_result = 0;
    n_zero = 0;
    memset(zero_order, 0, sizeof(zero_order));
    memset(segmap, 0, sizeof(segmap));
    memset(ppn_array, 0, sizeof(ppn_array));
}

/*
 * flags bit 1 set: the segment-map pointer we were handed goes straight to
 * ast_$clear_transition_bits, the status is set, nothing is allocated, and
 * the ORIGINAL count comes back.
 */
static void test_read_only_path(void)
{
    status_$t status = status_$ok;
    int16_t r;

    reset_all();

    r = ast_$count_valid_pages(&segmap[3], 5, 0x0002, ppn_array, &status);

    CHECK(n_clear_trans == 1);
    CHECK(last_clear_segmap == &segmap[3]);
    CHECK(last_clear_count == 5);
    CHECK(status == (status_$t)0x00050008);
    CHECK(n_alloc == 0);
    CHECK(n_zero == 0);
    CHECK(r == 5);

    printf("test_read_only_path: PASSED\n");
}

/* Only bit 1 selects the read-only path; the other flag bits do not. */
static void test_other_flag_bits_do_not_trigger_read_only(void)
{
    status_$t status = status_$ok;

    reset_all();
    alloc_result = 0;

    (void)ast_$count_valid_pages(&segmap[0], 3, 0xFFFD, ppn_array, &status);

    CHECK(n_clear_trans == 0);
    CHECK(n_alloc == 1);
    CHECK(status == status_$ok);

    printf("test_other_flag_bits_do_not_trigger_read_only: PASSED\n");
}

/*
 * The allocation arguments: the requested count is the FIRST word and the
 * minimum is 1 (`move.w #0x1,-(SP)` at 0x00E03094).
 */
static void test_allocate_argument_order(void)
{
    status_$t status = status_$ok;

    reset_all();
    alloc_result = 0;

    (void)ast_$count_valid_pages(&segmap[0], 7, 0, ppn_array, &status);

    CHECK(n_alloc == 1);
    CHECK(last_alloc_count == 7);
    CHECK(last_alloc_min == 1);
    CHECK(last_alloc_array == ppn_array);

    printf("test_allocate_argument_order: PASSED\n");
}

/* Nothing allocated -> no zeroing, and the zero result is returned. */
static void test_no_pages_allocated(void)
{
    status_$t status = status_$ok;
    int16_t r;

    reset_all();
    alloc_result = 0;

    r = ast_$count_valid_pages(&segmap[0], 4, 0, ppn_array, &status);

    CHECK(r == 0);
    CHECK(n_zero == 0);

    printf("test_no_pages_allocated: PASSED\n");
}

/*
 * The zero loop runs over ppn_array[0..allocated-1] in ASCENDING order:
 * the original post-increments A3.
 */
static void test_zero_order_is_ascending(void)
{
    status_$t status = status_$ok;
    int16_t r;

    reset_all();
    alloc_result = 3;
    ppn_array[0] = 0x100;
    ppn_array[1] = 0x200;
    ppn_array[2] = 0x300;
    ppn_array[3] = 0x400;   /* must not be touched */

    r = ast_$count_valid_pages(&segmap[0], 3, 0, ppn_array, &status);

    CHECK(r == 3);
    CHECK(n_zero == 3);
    CHECK(zero_order[0] == 0x100);
    CHECK(zero_order[1] == 0x200);
    CHECK(zero_order[2] == 0x300);

    printf("test_zero_order_is_ascending: PASSED\n");
}

/*
 * The count of pages zeroed follows what was ALLOCATED, not what was
 * requested (D3 is loaded from D0, not from the argument).
 */
static void test_zero_count_follows_allocation(void)
{
    status_$t status = status_$ok;

    reset_all();
    alloc_result = 2;

    (void)ast_$count_valid_pages(&segmap[0], 6, 0, ppn_array, &status);

    CHECK(last_alloc_count == 6);
    CHECK(n_zero == 2);

    printf("test_zero_count_follows_allocation: PASSED\n");
}

int main(void)
{
    printf("Running ast_$count_valid_pages tests...\n");

    test_read_only_path();
    test_other_flag_bits_do_not_trigger_read_only();
    test_allocate_argument_order();
    test_no_pages_allocated();
    test_zero_order_is_ascending();
    test_zero_count_follows_allocation();

    if (tests_failed != 0) {
        printf("%d checks failed\n", tests_failed);
        return 1;
    }
    printf("All tests PASSED!\n");
    return 0;
}
