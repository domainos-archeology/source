/*
 * audit/test/test_hash_table.c - unit tests for audit_$clear_hash_table
 * (0x00E7128A) and audit_$add_to_hash (0x00E712BA).
 *
 * Bead source-fxlx: audit_$clear_hash_table is a nested procedure of
 * audit_$load_list.  It reaches its parent's frame with `movea.l (A6),A2`
 * (0x00E71290) and pushes the parent's OWN status_ret, the longword at
 * A2+0x08, as audit_$alloc's second argument (0x00E71294).  Flattened here
 * with that uplevel reference as an explicit parameter, so the test pins
 * down that the pointer reaches audit_$alloc unchanged.
 *
 * The tests also pin the clear loop's slot range: 0x00E712A0-0x00E712AE
 * clears 37 longwords starting at A5+0xB4, i.e. bucket slots 1..37, while
 * UID_$HASH's remainder (the modulus is 37) selects slots 0..36.
 *
 * Bead source-3ad7 added audit_$alloc (0x00E7120C) itself, so the file also
 * exercises the bump allocator: the reset arm, the sign-extended advance,
 * the one-page-at-a-time WP_$CALLOC / MMU_$INSTALL wiring loop and the fact
 * that neither arm ever stores status_$ok.  ARCH_HOST_VA_BASE is aimed at a
 * local arena so that AUDIT_POOL_BASE_VA lands on real host memory.
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

#include "audit/audit_internal.h"
#include "mmu/mmu.h"
#include "wp/wp.h"

/* ------------------------------------------------------------------ */

audit_data_t AUDIT_$DATA;
int8_t       AUDIT_$ENABLED;
int8_t       AUDIT_$CORRUPTED;
uid_t        UID_$NIL = { 0, 0 };
uint32_t     NODE_$ME;

const int16_t audit_$hash_modulus = AUDIT_HASH_TABLE_SIZE;

/*
 * 0x00E17360 UID_$HASH: fold the two UID longwords together, fold the
 * halves, then `divu.w` and `swap` - the value that reaches the caller is
 * the REMAINDER, so 0 .. modulus-1.
 */
uint32_t UID_$HASH(uid_t *uid, uint16_t *modulus)
{
    uint32_t v = uid->low ^ uid->high;
    uint16_t folded = (uint16_t)(v ^ (v >> 16));
    return (uint32_t)(folded % *modulus);
}


/*
 * The allocator's arena.  ARCH_HOST_VA_BASE is set so that
 * AUDIT_POOL_BASE_VA (0x00EC4800) maps to arena[0]; three pages is more than
 * any test here consumes.
 */
#define AUDIT_TEST_ARENA_PAGES  3
static uint8_t audit_arena[AUDIT_TEST_ARENA_PAGES * 0x400];

static void audit_arena_install(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)audit_arena - (uintptr_t)AUDIT_POOL_BASE_VA;
}

/* WP_$CALLOC / MMU_$INSTALL call log. */
static int      wp_calloc_calls;
static int      mmu_install_calls;
static uint32_t mmu_install_ppn[8];
static uint32_t mmu_install_va[8];
static uint32_t mmu_install_flags[8];
static status_$t wp_calloc_status = status_$ok;

void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    *ppn_out = 0x1000u + (uint32_t)wp_calloc_calls;
    *status  = wp_calloc_status;
    wp_calloc_calls++;
}

void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    if (mmu_install_calls < 8) {
        mmu_install_ppn[mmu_install_calls]   = ppn;
        mmu_install_va[mmu_install_calls]    = va;
        mmu_install_flags[mmu_install_calls] = flags;
    }
    mmu_install_calls++;
}

static void audit_alloc_reset_state(void)
{
    audit_arena_install();
    wp_calloc_calls   = 0;
    mmu_install_calls = 0;
    wp_calloc_status  = status_$ok;
    AUDIT_$DATA.pool_next  = 0;
    AUDIT_$DATA.pool_limit = 0;
}

#include "../hash_table.c"

/* ------------------------------------------------------------------ */

/*
 * The clear loop writes slots 1..37 and no others: slot 0 is left alone
 * (an original off-by-one) and slot 37, one past the last hash value, is
 * cleared.
 */
TEST(clear_writes_slots_1_through_37)
{
    status_$t status = 0x11223344;
    audit_hash_node_t sentinel;
    int i;

    for (i = 0; i < AUDIT_HASH_TABLE_SLOTS; i++) {
        AUDIT_$DATA.hash_buckets[i] = &sentinel;
    }

    audit_$clear_hash_table(&status);

    ASSERT_TRUE(AUDIT_$DATA.hash_buckets[0] == &sentinel);
    for (i = 1; i <= AUDIT_HASH_TABLE_SIZE; i++) {
        ASSERT_TRUE(AUDIT_$DATA.hash_buckets[i] == NULL);
    }
    ASSERT_EQ(AUDIT_HASH_TABLE_SIZE + 1, AUDIT_HASH_TABLE_SLOTS);
}

/*
 * source-fxlx: the parent's status cell is the one audit_$alloc receives.
 * source-3ad7: the reset arm (0x00E7121E-0x00E71234) never stores anything
 * through it, so the caller's cell must come back EXACTLY as it went in.
 */
TEST(clear_leaves_the_callers_status_cell_alone)
{
    status_$t status = 0x11223344;

    audit_alloc_reset_state();
    audit_$clear_hash_table(&status);

    ASSERT_EQ(0x11223344, status);
    ASSERT_EQ(0, wp_calloc_calls);
}

/*
 * 0x00E712FE-0x00E71302: bucket address is (0xb0,A5) + hash*4, and the
 * hash is UID_$HASH's remainder, so an entry can land in slot 0 - the one
 * slot the clear loop never touches.
 */
TEST(add_uses_the_remainder_as_the_slot)
{
    status_$t status = status_$ok;
    uid_t uid;
    int i;
    int found = -1;

    audit_alloc_reset_state();
    for (i = 0; i < AUDIT_HASH_TABLE_SLOTS; i++) {
        AUDIT_$DATA.hash_buckets[i] = NULL;
    }
    audit_$clear_hash_table(&status);

    uid.high = 0;
    uid.low  = 0;                       /* folds to 0, 0 % 37 == 0 */
    audit_$add_to_hash(&uid, &status);
    ASSERT_EQ(status_$ok, status);

    for (i = 0; i < AUDIT_HASH_TABLE_SLOTS; i++) {
        if (AUDIT_$DATA.hash_buckets[i] != NULL) {
            found = i;
        }
    }
    ASSERT_EQ(0, found);
    ASSERT_EQ(0u, AUDIT_$DATA.hash_buckets[0]->uid_high);
    ASSERT_EQ(0u, AUDIT_$DATA.hash_buckets[0]->uid_low);
    ASSERT_TRUE(AUDIT_$DATA.hash_buckets[0]->next == NULL);
}


/* ------------------------------------------------------------------ */
/* audit_$alloc (0x00E7120C)                                          */

/*
 * 0x00E7121E: alloc(0) resets pool_next to AUDIT_POOL_BASE_VA and returns
 * NIL.  0x00E71226: pool_limit follows only while it is still zero.
 */
TEST(alloc_zero_resets_the_cursor)
{
    status_$t status = 0x55667788;
    void *p;

    audit_alloc_reset_state();

    p = audit_$alloc(0, &status);

    ASSERT_TRUE(p == NULL);
    ASSERT_EQ(AUDIT_POOL_BASE_VA, AUDIT_$DATA.pool_next);
    ASSERT_EQ(AUDIT_POOL_BASE_VA, AUDIT_$DATA.pool_limit);
    ASSERT_EQ(0x55667788, status);
}

/*
 * 0x00E71226 `tst.l (0x1a8,A5)` / `bne.b`: a second reset rewinds pool_next
 * but leaves the already-wired pool_limit where it is.
 */
TEST(alloc_zero_keeps_an_established_limit)
{
    status_$t status = status_$ok;

    audit_alloc_reset_state();
    audit_$alloc(0, &status);
    audit_$alloc(0x0C, &status);        /* wires one page, limit -> +0x400 */

    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x400,
              AUDIT_$DATA.pool_limit);

    audit_$alloc(0, &status);

    ASSERT_EQ(AUDIT_POOL_BASE_VA, AUDIT_$DATA.pool_next);
    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x400,
              AUDIT_$DATA.pool_limit);
}

/*
 * 0x00E71238-0x00E7123E: the old cursor is the result and the cursor moves
 * on by the size; 0x00E71244-0x00E7126C wires exactly one page here, at the
 * old pool_limit, with flags 0x16.
 */
TEST(alloc_hands_out_the_old_cursor_and_wires_a_page)
{
    status_$t status = status_$ok;
    void *first, *second;

    audit_alloc_reset_state();
    audit_$alloc(0, &status);

    first = audit_$alloc(0x0C, &status);

    ASSERT_EQ(AUDIT_POOL_BASE_VA, ARCH_PTR_TO_VA(first));
    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x0C, AUDIT_$DATA.pool_next);
    ASSERT_EQ(1, wp_calloc_calls);
    ASSERT_EQ(1, mmu_install_calls);
    ASSERT_EQ(0x1000u, mmu_install_ppn[0]);
    ASSERT_EQ(AUDIT_POOL_BASE_VA, mmu_install_va[0]);
    ASSERT_EQ(0x16u, mmu_install_flags[0]);
    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x400, AUDIT_$DATA.pool_limit);

    /* The next block is contiguous and needs no further page. */
    second = audit_$alloc(0x0C, &status);
    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x0C, ARCH_PTR_TO_VA(second));
    ASSERT_EQ(1, wp_calloc_calls);
}

/*
 * 0x00E71274-0x00E7127C is a while loop, so a request that overshoots the
 * limit by more than a page wires as many pages as it takes.
 */
TEST(alloc_wires_every_page_the_request_crosses)
{
    status_$t status = status_$ok;

    audit_alloc_reset_state();
    audit_$alloc(0, &status);

    audit_$alloc(0x500, &status);       /* base .. base+0x500 */

    ASSERT_EQ(2, wp_calloc_calls);
    ASSERT_EQ(AUDIT_POOL_BASE_VA,         mmu_install_va[0]);
    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x400, mmu_install_va[1]);
    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x800, AUDIT_$DATA.pool_limit);
}

/*
 * 0x00E7123C `ext.l D1`: the size word is SIGN-extended, so a negative size
 * walks the cursor backwards and the wiring loop does not run.
 */
TEST(alloc_sign_extends_the_size_word)
{
    status_$t status = status_$ok;
    void *p;

    audit_alloc_reset_state();
    audit_$alloc(0, &status);
    audit_$alloc(0x0C, &status);        /* limit -> base+0x400 */
    wp_calloc_calls = 0;

    p = audit_$alloc((uint16_t)-4, &status);

    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x0C, ARCH_PTR_TO_VA(p));
    ASSERT_EQ(AUDIT_POOL_BASE_VA + 0x08, AUDIT_$DATA.pool_next);
    ASSERT_EQ(0, wp_calloc_calls);
}

/*
 * 0x00E71252-0x00E71254: a failed WP_$CALLOC returns the block that was
 * already handed out, leaves pool_limit alone and installs nothing.
 */
TEST(alloc_returns_the_block_when_wiring_fails)
{
    status_$t status = status_$ok;
    void *p;

    audit_alloc_reset_state();
    audit_$alloc(0, &status);
    wp_calloc_status = 0x00120007;

    p = audit_$alloc(0x0C, &status);

    ASSERT_EQ(AUDIT_POOL_BASE_VA, ARCH_PTR_TO_VA(p));
    ASSERT_EQ(0x00120007, status);
    ASSERT_EQ(1, wp_calloc_calls);
    ASSERT_EQ(0, mmu_install_calls);
    ASSERT_EQ(AUDIT_POOL_BASE_VA, AUDIT_$DATA.pool_limit);
}

int main(void)
{
    printf("audit hash table tests\n");
    RUN_TEST(clear_writes_slots_1_through_37);
    RUN_TEST(clear_leaves_the_callers_status_cell_alone);
    RUN_TEST(add_uses_the_remainder_as_the_slot);
    RUN_TEST(alloc_zero_resets_the_cursor);
    RUN_TEST(alloc_zero_keeps_an_established_limit);
    RUN_TEST(alloc_hands_out_the_old_cursor_and_wires_a_page);
    RUN_TEST(alloc_wires_every_page_the_request_crosses);
    RUN_TEST(alloc_sign_extends_the_size_word);
    RUN_TEST(alloc_returns_the_block_when_wiring_fails);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
