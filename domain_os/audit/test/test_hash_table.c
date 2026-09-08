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
 * source-fxlx: the parent's status cell is the one audit_$alloc writes.
 * audit_$alloc(0, ...) always reports status_$ok, so the caller's cell must
 * come back cleared rather than holding its previous value.
 */
TEST(clear_forwards_the_callers_status_cell)
{
    status_$t status = 0x11223344;

    audit_$clear_hash_table(&status);

    ASSERT_EQ(status_$ok, status);
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

int main(void)
{
    printf("audit hash table tests\n");
    RUN_TEST(clear_writes_slots_1_through_37);
    RUN_TEST(clear_forwards_the_callers_status_cell);
    RUN_TEST(add_uses_the_remainder_as_the_slot);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
