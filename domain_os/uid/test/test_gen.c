/*
 * uid/test/test_gen.c - unit tests for UID_$GEN (0x00E1A018)
 *
 * ML_$SPIN_LOCK / ML_$SPIN_UNLOCK and TIME_$ABS_CLOCK are mocked; TIME_$CLOCKH
 * and the generator cells are defined here.  The wait-loop reference nibble
 * (bead source-ilw0) is 0 in the C, so the "clock has not advanced" path is
 * driven by a state whose counter nibble is 0.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-52s ", #name);                  \
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

#include "uid/uid_internal.h"
#include "time/time.h"
#include "ml/ml.h"

uint32_t TIME_$CLOCKH;
uid_t    UID_$GENERATOR_STATE;
uint16_t UID_$GENERATOR_LOCK;

static int lock_calls, unlock_calls, lock_depth;
static void *lock_ptr_seen;
static ml_$spin_token_t token_seen;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    lock_calls++;
    lock_depth++;
    lock_ptr_seen = lockp;
    return (ml_$spin_token_t)(0x2700 + lock_calls);
}

void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot)
{
    ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token;
    (void)lockp;
    unlock_calls++;
    lock_depth--;
    token_seen = token;
}

/* TIME_$ABS_CLOCK: scripted (high, low) per call; the state's counter
 * nibble is bumped from the mock when `bump_on_call` matches, standing in
 * for another process using the generator while the lock is dropped. */
static int      abs_calls;
static uint32_t abs_high[8];
static uint16_t abs_low[8];
static int      bump_on_call;

void TIME_$ABS_CLOCK(clock_t *clock)
{
    int n = abs_calls++;
    clock->high = abs_high[n < 8 ? n : 7];
    clock->low  = abs_low[n < 8 ? n : 7];
    if (bump_on_call == n) {
        UID_$GENERATOR_STATE.low += 0x10000000u;
    }
}

#include "../gen.c"

static void reset(void)
{
    TIME_$CLOCKH = 0x1000;
    UID_$GENERATOR_STATE.high = 0;
    UID_$GENERATOR_STATE.low = 0;
    UID_$GENERATOR_LOCK = 0;
    lock_calls = unlock_calls = lock_depth = 0;
    abs_calls = 0;
    memset(abs_high, 0, sizeof(abs_high));
    memset(abs_low, 0, sizeof(abs_low));
    bump_on_call = -1;
}

/* 0x00E1A042 bls not taken: clock - 0xF0 above the stored high word */
TEST(clock_advanced_takes_new_high)
{
    uid_t out = { 0, 0 };

    reset();
    UID_$GENERATOR_STATE.high = 0x0E00;
    UID_$GENERATOR_STATE.low  = 0x300ABCDE;
    UID_$GEN(&out);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ((uintptr_t)&UID_$GENERATOR_LOCK, (uintptr_t)lock_ptr_seen);
    ASSERT_EQ(0x2701, token_seen);
    ASSERT_EQ(0, abs_calls);
    ASSERT_EQ(0x0F10, out.high);            /* 0x1000 - 0xF0 */
    ASSERT_EQ(0x300ABCDE, out.low);         /* copied BEFORE the bump */
    ASSERT_EQ(0x0F10, UID_$GENERATOR_STATE.high);
    ASSERT_EQ(0x400ABCDE, UID_$GENERATOR_STATE.low);
}

/* equal clock is NOT "advanced" (bls includes equality) */
TEST(equal_clock_waits_path_then_high_differs)
{
    uid_t out = { 0, 0 };

    reset();
    UID_$GENERATOR_STATE.high = 0x0F10;
    UID_$GENERATOR_STATE.low  = 0x500ABCDE;
    abs_high[0] = 0x0F11;                   /* != state.high -> break at once */
    UID_$GEN(&out);

    ASSERT_EQ(1, abs_calls);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x0F10, out.high);
    ASSERT_EQ(0x500ABCDE, out.low);
    ASSERT_EQ(0x600ABCDE, UID_$GENERATOR_STATE.low);
}

/* same high word but the counter nibble (5) differs from the reference (0) */
TEST(nibble_differs_breaks_without_waiting)
{
    uid_t out = { 0, 0 };

    reset();
    UID_$GENERATOR_STATE.high = 0x0F10;
    UID_$GENERATOR_STATE.low  = 0x500ABCDE;
    abs_high[0] = 0x0F10;
    UID_$GEN(&out);
    ASSERT_EQ(1, abs_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x600ABCDE, UID_$GENERATOR_STATE.low);
}

/* nibble equals the reference: unlock, spin until it moves, relock, re-check */
TEST(nibble_equal_spins_unlocked)
{
    uid_t out = { 0, 0 };

    reset();
    UID_$GENERATOR_STATE.high = 0x0F10;
    UID_$GENERATOR_STATE.low  = 0x000ABCDE;     /* nibble 0 == reference 0 */
    abs_high[0] = 0x0F10;                       /* first check: same high */
    abs_high[1] = 0x0F10;                       /* inner spin, call 1: bump here */
    abs_high[2] = 0x0F10;                       /* re-check after relock */
    bump_on_call = 1;
    UID_$GEN(&out);

    ASSERT_EQ(3, abs_calls);
    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(2, unlock_calls);                 /* wait unlock + final unlock */
    ASSERT_EQ(0, lock_depth);
    ASSERT_EQ(0x2702, token_seen);              /* final unlock uses the 2nd token */
    ASSERT_EQ(0x100ABCDE, out.low);             /* the bumped state was copied */
    ASSERT_EQ(0x200ABCDE, UID_$GENERATOR_STATE.low);
}

/* 0x00E1A0C6-0x00E1A0D2: nibble wrap carries into the high word */
TEST(counter_wrap_increments_high)
{
    uid_t out = { 0, 0 };

    reset();
    UID_$GENERATOR_STATE.high = 0x0E00;
    UID_$GENERATOR_STATE.low  = 0xF00ABCDE;
    UID_$GEN(&out);
    ASSERT_EQ(0x0F10, out.high);
    ASSERT_EQ(0xF00ABCDE, out.low);
    ASSERT_EQ(0x000ABCDE, UID_$GENERATOR_STATE.low);
    ASSERT_EQ(0x0F11, UID_$GENERATOR_STATE.high);
}

int main(void)
{
    printf("UID_$GEN tests\n");
    RUN_TEST(clock_advanced_takes_new_high);
    RUN_TEST(equal_clock_waits_path_then_high_differs);
    RUN_TEST(nibble_differs_breaks_without_waiting);
    RUN_TEST(nibble_equal_spins_unlocked);
    RUN_TEST(counter_wrap_increments_high);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
