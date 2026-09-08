/*
 * pkt/test/test_next_id.c - PKT_$NEXT_ID (0x00E1248E).
 *
 * The property under test is the wrap:
 *
 *   00e124b4  cmpi.w #-0x600,(0x5c,A5)
 *   00e124ba  bls.b 0x00e124c2
 *   00e124bc  move.w #0x1,(0x5c,A5)
 *
 * `bls` is the UNSIGNED low-or-same branch, so the counter is reset once it
 * passes 0xFA00 = 64000.  With the counter declared int16_t the comparison
 * could never be true (64000 does not fit a signed word) and the generator
 * would walk on into 0x8000 and beyond instead of cycling 1..64000.
 *
 * Also pinned: the value returned is the value BEFORE the bump
 * (0x00E124AC/0x00E124B0), and the spin lock is taken and released around the
 * whole update (0x00E124A0 / 0x00E124CA) with the token from D0.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals and mocks
 * ========================================================================== */

#include "pkt/pkt_internal.h"

int8_t NETWORK_$LOOPBACK_FLAG;

#include "../pkt_data.c"

static int lock_calls;
static int unlock_calls;
static void *lock_seen;
static void *unlock_seen;
static ml_$spin_token_t unlock_token_seen;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    lock_calls++;
    lock_seen = lockp;
    return 0x1357;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    unlock_calls++;
    unlock_seen = lockp;
    unlock_token_seen = token;
}

#include "../next_id.c"

/* ==========================================================================
 * Tests
 * ========================================================================== */

static void reset(uint16_t start)
{
    PKT_$DATA->short_id = start;
    lock_calls = 0;
    unlock_calls = 0;
}

/* The returned id is the value before the increment. */
TEST(returns_the_previous_value_and_bumps)
{
    int16_t id;

    reset(7);
    id = PKT_$NEXT_ID();

    ASSERT_EQ(7, id);
    ASSERT_EQ(8, PKT_$DATA->short_id);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x1357, unlock_token_seen);
    /* both sides address the same cell, PKT_$DATA + 0x50 */
    ASSERT_EQ((long long)(size_t)lock_seen, (long long)(size_t)unlock_seen);
    ASSERT_EQ((long long)(size_t)&PKT_$DATA->spin_lock,
              (long long)(size_t)lock_seen);
}

/* 64000 is still legal: the counter becomes exactly 64000 and is left alone. */
TEST(sixty_four_thousand_is_not_wrapped)
{
    int16_t id;

    reset(63999);
    id = PKT_$NEXT_ID();

    ASSERT_EQ((int16_t)63999, id);
    ASSERT_EQ(64000u, PKT_$DATA->short_id);
}

/* One past 64000 resets to 1 (0x00E124BC move.w #0x1). */
TEST(sixty_four_thousand_and_one_wraps_to_one)
{
    int16_t id;

    reset(64000);
    id = PKT_$NEXT_ID();

    ASSERT_EQ((int16_t)64000, id);
    ASSERT_EQ(1u, PKT_$DATA->short_id);
}

/*
 * The regression the bead names: at 0x7FFF the signed reading of the counter
 * would go negative and stay there.  Unsigned, 0x8000 is simply below 0xFA00
 * and the counter keeps climbing.
 */
TEST(crossing_the_signed_word_boundary_does_not_wrap)
{
    int16_t id;

    reset(0x7FFF);
    id = PKT_$NEXT_ID();

    ASSERT_EQ((int16_t)0x7FFF, id);
    ASSERT_EQ(0x8000u, PKT_$DATA->short_id);

    /* and it keeps going through the whole 0x8000..0xFA00 range */
    reset(0xF9FF);
    id = PKT_$NEXT_ID();
    ASSERT_EQ((int16_t)0xF9FF, id);
    ASSERT_EQ(0xFA00u, PKT_$DATA->short_id);
}

/* A value already past the limit is pulled back on the next call. */
TEST(a_value_past_the_limit_is_reset)
{
    reset(0xFFFE);
    (void)PKT_$NEXT_ID();
    ASSERT_EQ(1u, PKT_$DATA->short_id);
}

int main(void)
{
    printf("PKT_$NEXT_ID tests\n");
    RUN_TEST(returns_the_previous_value_and_bumps);
    RUN_TEST(sixty_four_thousand_is_not_wrapped);
    RUN_TEST(sixty_four_thousand_and_one_wraps_to_one);
    RUN_TEST(crossing_the_signed_word_boundary_does_not_wrap);
    RUN_TEST(a_value_past_the_limit_is_reset);

    printf("\n%d test(s) run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
