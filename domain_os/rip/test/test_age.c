/*
 * rip/test/test_age.c - unit tests for RIP_$AGE (0x00E155C0)
 *
 * Bead source-9oyx.  Two properties of the image that a straight reading of
 * the decompiler output loses:
 *
 *   SLOT ORDER  the inner loop visits entry+0x18 (routes[1]) BEFORE
 *               entry+0x04 (routes[0]).  The `bne.b` at 0x00E155E2 tests the
 *               flags from `clr.w D2w` / `addq.w #0x1,D2w`, so the first pass
 *               falls through to `st D4b / lea (0x17c,A2)` at 0x00E155E4 and
 *               the second takes `clr.b D4b / lea (0x168,A2)` at 0x00E155EC.
 *               D4 is what picks std_recent_changes (0x00E15642) over
 *               recent_changes (0x00E15648).
 *
 *   CLOCK       TIME_$CLOCKH is held as an ADDRESS in A0 (0x00E155D4) and
 *               re-read at 0x00E15600, 0x00E15620 and 0x00E1564C, so a tick
 *               between the comparison and the re-arm shows up in the stored
 *               expiry.
 */

#include <stdio.h>
#include <string.h>

#include "time/time.h"     /* TIME_$CLOCKH_EC / TIME_$CLOCKH */
#include "rip/rip_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-56s ", #name);                                            \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed == 0) { printf("PASSED\n"); }                      \
    } while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
        unsigned long long _e = (unsigned long long)(expected);               \
        unsigned long long _a = (unsigned long long)(actual);                 \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",    \
                   _e, _a, __LINE__);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * Globals and mocks
 * ============================================================================ */

MODULE_DATA_DEFINE(rip_$wired_data_t, RIP_$WIRED_DATA, 0x00E26258);
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

static int lock_calls;
static int unlock_calls;

void RIP_$LOCK(void)   { lock_calls++; }
void RIP_$UNLOCK(void) { unlock_calls++; }

static int      send_calls;
static boolean  send_arg[4];

void RIP_$SEND_UPDATES(boolean is_std)
{
    if (send_calls < 4) {
        send_arg[send_calls] = is_std;
    }
    send_calls++;
}

/*
 * A clock the test can watch.  RIP_$AGE holds TIME_$CLOCKH as an ADDRESS in
 * A0 (0x00E155D4 `movea.l #0xe2b0d4,A0`) and dereferences it afresh at
 * 0x00E15600, 0x00E15620 and 0x00E1564C, so redirecting the global to a
 * counting accessor makes both the number of reads and their order visible.
 * When clock_step is zero the accessor is an ordinary read of the global.
 */
static int      clock_reads;
static uint32_t clock_step;

static uint32_t rip_test_clock(void)
{
    uint32_t v = TIME_$CLOCKH;

    clock_reads++;
    TIME_$CLOCKH += clock_step;
    return v;
}

#define TIME_$CLOCKH rip_test_clock()

#include "../age.c"

#undef TIME_$CLOCKH
#define TIME_$CLOCKH (*(uint32_t *)&TIME_$CLOCKH_EC.value)   /* back to time.h's view */

/* ============================================================================
 * Helpers
 * ============================================================================ */

static void reset(void)
{
    memset(&RIP_$WIRED_DATA, 0, sizeof(RIP_$WIRED_DATA));
    lock_calls = unlock_calls = send_calls = 0;
    memset(send_arg, 0, sizeof(send_arg));
    TIME_$CLOCKH = 0;
    clock_reads = 0;
    clock_step = 0;
}

static void arm(int entry_idx, int slot, uint8_t state, uint8_t metric,
                uint32_t expiration)
{
    rip_$route_t *r = &RIP_$WIRED_DATA.info[entry_idx].routes[slot];

    r->flags      = (uint8_t)(state << RIP_STATE_SHIFT);
    r->metric     = metric;
    r->expiration = expiration;
}

static uint8_t state_of(int entry_idx, int slot)
{
    return (uint8_t)((RIP_$WIRED_DATA.info[entry_idx].routes[slot].flags &
                      RIP_STATE_MASK) >> RIP_STATE_SHIFT);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* 0x00E155CE / 0x00E15672: exactly one lock and one unlock per call. */
TEST(locks_once_and_sends_both_updates)
{
    reset();
    RIP_$AGE();

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    /* 0x00E15678 `clr.w -(SP)` then 0x00E15684 `st -(SP)`. */
    ASSERT_EQ(2, send_calls);
    ASSERT_EQ(0, send_arg[0]);
    ASSERT_EQ((boolean)-1, send_arg[1]);
}

/* 0x00E15616-0x00E15636: VALID + metric != 0 -> AGING with a new expiry. */
TEST(valid_route_ages)
{
    reset();
    TIME_$CLOCKH = 1000;
    arm(3, 0, RIP_STATE_VALID, 5, 999);
    RIP_$AGE();

    ASSERT_EQ(RIP_STATE_AGING, state_of(3, 0));
    ASSERT_EQ(1000u + RIP_ROUTE_TIMEOUT, RIP_$WIRED_DATA.info[3].routes[0].expiration);
}

/* 0x00E1561C: a metric of 0 leaves the slot completely alone. */
TEST(direct_route_never_ages)
{
    reset();
    TIME_$CLOCKH = 1000;
    arm(3, 0, RIP_STATE_VALID, 0, 999);
    RIP_$AGE();

    ASSERT_EQ(RIP_STATE_VALID, state_of(3, 0));
    ASSERT_EQ(999u, RIP_$WIRED_DATA.info[3].routes[0].expiration);
}

/* 0x00E15602: the comparison is <=, so an expiry equal to now does not fire. */
TEST(expiry_equal_to_now_does_not_fire)
{
    reset();
    TIME_$CLOCKH = 1000;
    arm(3, 0, RIP_STATE_VALID, 5, 1000);
    RIP_$AGE();

    ASSERT_EQ(RIP_STATE_VALID, state_of(3, 0));
}

/* 0x00E1565E: any state above 2 goes straight back to UNUSED. */
TEST(expired_route_becomes_unused)
{
    reset();
    TIME_$CLOCKH = 1000;
    arm(3, 0, RIP_STATE_EXPIRED, 5, 999);
    RIP_$AGE();

    ASSERT_EQ(RIP_STATE_UNUSED, state_of(3, 0));
}

/*
 * source-9oyx: routes[1] is the STANDARD slot (D4 true -> 0x00E15642
 * `st (0xc86,A5)`) and routes[0] the non-standard one (0x00E15648
 * `st (0xc88,A5)`).
 */
TEST(change_flags_follow_the_slot)
{
    reset();
    TIME_$CLOCKH = 1000;
    arm(7, 1, RIP_STATE_AGING, 5, 999);
    RIP_$AGE();
    ASSERT_EQ(-1, RIP_$WIRED_DATA.std_recent_changes);
    ASSERT_EQ(0x00, RIP_$WIRED_DATA.recent_changes);
    ASSERT_EQ(RIP_INFINITY, RIP_$WIRED_DATA.info[7].routes[1].metric);
    ASSERT_EQ(RIP_STATE_EXPIRED, state_of(7, 1));

    reset();
    TIME_$CLOCKH = 1000;
    arm(7, 0, RIP_STATE_AGING, 5, 999);
    RIP_$AGE();
    ASSERT_EQ(0x00, RIP_$WIRED_DATA.std_recent_changes);
    ASSERT_EQ(-1, RIP_$WIRED_DATA.recent_changes);
}

/*
 * source-9oyx: the visit order is routes[1] then routes[0], and the clock is
 * re-read per route.  With a clock that advances one tick per read, the four
 * reads this arrangement makes are
 *
 *   1  the comparison for routes[1]   (0x00E15600)
 *   2  the re-arm for routes[1]       (0x00E15620)
 *   3  the comparison for routes[0]
 *   4  the re-arm for routes[0]
 *
 * so routes[1] gets the earlier expiry.  A cached clock would give both the
 * same value, and the opposite visit order would swap them.
 */
TEST(slot_one_is_visited_before_slot_zero)
{
    reset();
    TIME_$CLOCKH = 1000;
    clock_step = 1;
    arm(9, 0, RIP_STATE_VALID, 5, 100);
    arm(9, 1, RIP_STATE_VALID, 5, 100);
    RIP_$AGE();

    ASSERT_EQ(4, clock_reads);
    ASSERT_EQ(1001u + RIP_ROUTE_TIMEOUT, RIP_$WIRED_DATA.info[9].routes[1].expiration);
    ASSERT_EQ(1003u + RIP_ROUTE_TIMEOUT, RIP_$WIRED_DATA.info[9].routes[0].expiration);
}

/*
 * The same for the AGING arm, whose re-arm is the read at 0x00E1564C: two
 * reads per slot again, and routes[1] still first.
 */
TEST(aging_arm_also_re_reads_the_clock)
{
    reset();
    TIME_$CLOCKH = 1000;
    clock_step = 1;
    arm(9, 0, RIP_STATE_AGING, 5, 100);
    arm(9, 1, RIP_STATE_AGING, 5, 100);
    RIP_$AGE();

    ASSERT_EQ(4, clock_reads);
    ASSERT_EQ(1001u + RIP_ROUTE_TIMEOUT, RIP_$WIRED_DATA.info[9].routes[1].expiration);
    ASSERT_EQ(1003u + RIP_ROUTE_TIMEOUT, RIP_$WIRED_DATA.info[9].routes[0].expiration);
}

/*
 * An unused slot costs no clock read at all: 0x00E155FE `beq` leaves before
 * the `move.l (A0),D3` at 0x00E15600.
 */
TEST(unused_slots_do_not_read_the_clock)
{
    reset();
    TIME_$CLOCKH = 1000;
    clock_step = 1;
    RIP_$AGE();

    ASSERT_EQ(0, clock_reads);
}

/* Every one of the 64 entries is visited (0x00E155D2 moveq #0x3f). */
TEST(all_sixty_four_entries_are_walked)
{
    int i;

    reset();
    TIME_$CLOCKH = 1000;
    for (i = 0; i < RIP_TABLE_SIZE; i++) {
        arm(i, 0, RIP_STATE_EXPIRED, 5, 1);
        arm(i, 1, RIP_STATE_EXPIRED, 5, 1);
    }
    RIP_$AGE();

    for (i = 0; i < RIP_TABLE_SIZE; i++) {
        ASSERT_EQ(RIP_STATE_UNUSED, state_of(i, 0));
        ASSERT_EQ(RIP_STATE_UNUSED, state_of(i, 1));
    }
}

int main(void)
{
    printf("RIP_$AGE (0x00E155C0) tests\n");
    RUN_TEST(locks_once_and_sends_both_updates);
    RUN_TEST(valid_route_ages);
    RUN_TEST(direct_route_never_ages);
    RUN_TEST(expiry_equal_to_now_does_not_fire);
    RUN_TEST(expired_route_becomes_unused);
    RUN_TEST(change_flags_follow_the_slot);
    RUN_TEST(slot_one_is_visited_before_slot_zero);
    RUN_TEST(aging_arm_also_re_reads_the_clock);
    RUN_TEST(unused_slots_do_not_read_the_clock);
    RUN_TEST(all_sixty_four_entries_are_walked);
    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
