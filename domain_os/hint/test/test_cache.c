/*
 * hint/test/test_cache.c - Unit tests for the HINT local cache trio:
 * HINT_$INIT_CACHE (0x00E313C8), HINT_$LOOKUP_CACHE (0x00E49D06) and
 * HINT_$ADD_CACHE (0x00E49D88).
 *
 * All three walk the cache with "moveq #0x1,D0 ... dbf D0w", i.e. exactly
 * HINT_CACHE_SIZE (2) iterations.  A third pass would run into
 * hint_globals_t.hintfile_uid at globals+0x18 and, on the write side, into
 * hintfile_ptr at globals+0x20, so the tests place a sentinel there and check
 * it survives.
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
 * Globals and mocks the code under test links against
 * ========================================================================== */

#include "hint/hint_internal.h"

/*
 * The whole HINT_ data segment as one object, so the cache entries, the
 * hint-file UID and the two round-robin indices sit at the offsets the image
 * uses.  The SAU2 link map gives the segment as "D E7DB50 HINT_ size = 28".
 */
hint_globals_t HINT_$GLOBALS_BLOCK;

hint_cache_entry_t *const HINT_$CACHE_PTR = HINT_$GLOBALS_BLOCK.cache;

/* The macros in hint_internal.h resolve these on the host. */
#define HINT_$CACHE (HINT_$GLOBALS->cache)
#define HINT_$CACHE_INDEX (HINT_$GLOBALS->cache_index)
#define HINT_$BUCKET_INDEX (HINT_$GLOBALS->bucket_index)
#define HINT_$EXCLUSION_LOCK (hint_test_lock)

static ml_$exclusion_t hint_test_lock;

uint32_t TIME_$CLOCKH;

static int excl_init_calls;
static int excl_start_calls;
static int excl_stop_calls;

void ML_$EXCLUSION_INIT(ml_$exclusion_t *excl)
{
    (void)excl;
    excl_init_calls++;
}

void ML_$EXCLUSION_START(ml_$exclusion_t *excl)
{
    (void)excl;
    excl_start_calls++;
}

void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)
{
    (void)excl;
    excl_stop_calls++;
}

#include "../init_cache.c"
#include "../lookup_cache.c"
#include "../add_cache.c"

/* ==========================================================================
 * Harness
 * ========================================================================== */

/* Poison value written over the whole globals block before each test. */
#define POISON 0xA5

static void poison_globals(void)
{
    memset(&HINT_$GLOBALS_BLOCK, POISON, sizeof(HINT_$GLOBALS_BLOCK));
    excl_init_calls = 0;
    excl_start_calls = 0;
    excl_stop_calls = 0;
}

/* The bytes that follow the two cache entries, i.e. globals+0x18 onwards. */
static int tail_is_poisoned(void)
{
    const uint8_t *p = (const uint8_t *)&HINT_$GLOBALS_BLOCK;
    size_t i;

    for (i = sizeof(hint_cache_entry_t) * HINT_CACHE_SIZE;
         i < __builtin_offsetof(hint_globals_t, cache_index); i++) {
        if (p[i] != POISON) {
            return 0;
        }
    }
    return 1;
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

/*
 * The bound and the overlap it protects, stated as the image states them:
 * two 12-byte entries fill globals+0x00..0x17 and entry 3 would start exactly
 * where hintfile_uid does.
 */
TEST(cache_layout)
{
    ASSERT_EQ(0x0C, sizeof(hint_cache_entry_t));
    ASSERT_EQ(2, HINT_CACHE_SIZE);
    ASSERT_EQ(0x18, sizeof(hint_cache_entry_t) * HINT_CACHE_SIZE);
    ASSERT_EQ(0x18, __builtin_offsetof(hint_globals_t, hintfile_uid));
    ASSERT_EQ(0x20, __builtin_offsetof(hint_globals_t, hintfile_ptr));
    ASSERT_EQ(0x24, __builtin_offsetof(hint_globals_t, cache_index));
    ASSERT_EQ(0x28, sizeof(hint_globals_t));
}

/* ==========================================================================
 * HINT_$INIT_CACHE
 * ========================================================================== */

/*
 * 0x00E313DA-0x00E313F8: "moveq #0x1,D0" / "dbf D0w" clears two entries and
 * only two; 0x00E31402 seeds the index with 1.
 */
TEST(init_cache_clears_two_entries)
{
    poison_globals();
    HINT_$INIT_CACHE();

    ASSERT_EQ(1, excl_init_calls);

    ASSERT_EQ(0, HINT_$CACHE[0].uid_low_masked);
    ASSERT_EQ(0, HINT_$CACHE[0].result);
    ASSERT_EQ(0, HINT_$CACHE[0].timestamp);
    ASSERT_EQ(0, HINT_$CACHE[1].uid_low_masked);
    ASSERT_EQ(0, HINT_$CACHE[1].result);
    ASSERT_EQ(0, HINT_$CACHE[1].timestamp);

    /* A third pass would have cleared bytes of hintfile_uid/hintfile_ptr. */
    ASSERT_EQ(1, tail_is_poisoned());

    ASSERT_EQ(1, HINT_$CACHE_INDEX);
}

/* ==========================================================================
 * HINT_$ADD_CACHE
 * ========================================================================== */

/* 0x00E49DB4: the first entry with a zero key wins. */
TEST(add_cache_fills_the_first_free_entry)
{
    uint32_t key = 0x000ABCDE;
    uint8_t result = 0x77;

    poison_globals();
    HINT_$INIT_CACHE();
    TIME_$CLOCKH = 0x11223344;

    HINT_$ADD_CACHE(&key, &result);

    ASSERT_EQ(1, excl_start_calls);
    ASSERT_EQ(1, excl_stop_calls);
    ASSERT_EQ(key, HINT_$CACHE[0].uid_low_masked);
    ASSERT_EQ(result, HINT_$CACHE[0].result);
    ASSERT_EQ(0x11223344, HINT_$CACHE[0].timestamp);

    /* Entry 1 is untouched, and the index has not moved. */
    ASSERT_EQ(0, HINT_$CACHE[1].uid_low_masked);
    ASSERT_EQ(1, HINT_$CACHE_INDEX);
}

/* The second add lands in entry 1, still without touching the index. */
TEST(add_cache_fills_the_second_free_entry)
{
    uint32_t key0 = 0x00011111;
    uint32_t key1 = 0x00022222;
    uint8_t r0 = 1, r1 = 2;

    poison_globals();
    HINT_$INIT_CACHE();
    TIME_$CLOCKH = 0x100;

    HINT_$ADD_CACHE(&key0, &r0);
    HINT_$ADD_CACHE(&key1, &r1);

    ASSERT_EQ(key0, HINT_$CACHE[0].uid_low_masked);
    ASSERT_EQ(key1, HINT_$CACHE[1].uid_low_masked);
    ASSERT_EQ(1, HINT_$CACHE_INDEX);
    ASSERT_EQ(1, tail_is_poisoned());
}

/*
 * The bound itself.  With both entries taken, the scan must NOT find a third
 * one: "moveq #0x1,D0" (0x00E49DAC) with "dbf D0w" (0x00E49DE0) runs the body
 * twice, so control falls through to the round-robin arm at 0x00E49DE4.  A
 * three-iteration scan would instead read globals+0x14 (inside hintfile_uid)
 * and, finding it non-zero here, would still fall through - so the test also
 * checks that nothing past the two entries was written.
 */
TEST(add_cache_scan_runs_exactly_twice)
{
    uint32_t key0 = 0x00011111;
    uint32_t key1 = 0x00022222;
    uint32_t key2 = 0x00033333;
    uint8_t r0 = 1, r1 = 2, r2 = 3;

    poison_globals();
    HINT_$INIT_CACHE();
    TIME_$CLOCKH = 0x200;

    HINT_$ADD_CACHE(&key0, &r0);
    HINT_$ADD_CACHE(&key1, &r1);

    /* Both slots are full: this add must take the round-robin path. */
    HINT_$ADD_CACHE(&key2, &r2);

    /* 0x00E49DE4: index 1 -> 2, still <= 2 so no wrap; entry 2 is replaced. */
    ASSERT_EQ(2, HINT_$CACHE_INDEX);
    ASSERT_EQ(key0, HINT_$CACHE[0].uid_low_masked);
    ASSERT_EQ(key2, HINT_$CACHE[1].uid_low_masked);
    ASSERT_EQ(r2, HINT_$CACHE[1].result);
    ASSERT_EQ(0x200, HINT_$CACHE[1].timestamp);

    /* Nothing beyond the two entries was written. */
    ASSERT_EQ(1, tail_is_poisoned());
}

/*
 * 0x00E49DE4-0x00E49DF4: "addq.w #0x1,(0x24,A5)" then
 * "cmpi.w #0x2,(0x24,A5) / ble" - the index cycles 1,2,1,2 and never reaches
 * 3, so the stores always land inside the two-entry cache.
 */
TEST(add_cache_round_robin_wraps_at_two)
{
    uint32_t key = 0x00044444;
    uint8_t r = 9;
    int i;

    poison_globals();
    HINT_$INIT_CACHE();
    TIME_$CLOCKH = 0x300;

    /* Fill both entries so every later add takes the round-robin path. */
    HINT_$ADD_CACHE(&key, &r);
    HINT_$ADD_CACHE(&key, &r);
    ASSERT_EQ(1, HINT_$CACHE_INDEX);

    for (i = 0; i < 6; i++) {
        HINT_$ADD_CACHE(&key, &r);
        /* 1 -> 2 -> 1 -> 2 ... */
        ASSERT_EQ((i % 2 == 0) ? 2 : 1, HINT_$CACHE_INDEX);
        ASSERT_EQ(1, tail_is_poisoned());
    }
}

/* ==========================================================================
 * HINT_$LOOKUP_CACHE
 * ========================================================================== */

/* 0x00E49D3E: a fresh match copies the result out and refreshes the stamp. */
TEST(lookup_cache_hit_refreshes)
{
    uint32_t key = 0x00055555;
    uint8_t r = 0x5A;
    uint8_t out = 0xFF;

    poison_globals();
    HINT_$INIT_CACHE();
    TIME_$CLOCKH = 1000;
    HINT_$ADD_CACHE(&key, &r);

    TIME_$CLOCKH = 1000 + HINT_CACHE_TIMEOUT - 1;
    HINT_$LOOKUP_CACHE(&key, &out);

    ASSERT_EQ(0x5A, out);
    ASSERT_EQ(1000 + HINT_CACHE_TIMEOUT - 1, HINT_$CACHE[0].timestamp);
}

/*
 * 0x00E49D4C "cmpi.l #0xf0,D3" / "bge.b 0x00E49D66": at exactly
 * HINT_CACHE_TIMEOUT the entry is expired, and the branch goes to the LOOP
 * STEP, not to the exit - so the scan carries on and the fall-through at
 * 0x00E49D70 clears the result byte.
 */
TEST(lookup_cache_expired_entry_keeps_scanning)
{
    uint32_t key0 = 0x00066666;
    uint32_t key1 = 0x00077777;
    uint8_t r0 = 0x11, r1 = 0x22;
    uint8_t out = 0xFF;

    poison_globals();
    HINT_$INIT_CACHE();

    TIME_$CLOCKH = 1000;
    HINT_$ADD_CACHE(&key0, &r0);
    HINT_$ADD_CACHE(&key1, &r1);

    /* Entry 0 is exactly at the timeout: expired. */
    TIME_$CLOCKH = 1000 + HINT_CACHE_TIMEOUT;
    HINT_$LOOKUP_CACHE(&key0, &out);

    /* The scan ran to the end and cleared the byte. */
    ASSERT_EQ(0, out);
    /* The expired entry keeps its old stamp - no refresh happened. */
    ASSERT_EQ(1000, HINT_$CACHE[0].timestamp);

    /*
     * The same expired key in entry 0 must not stop the scan reaching entry 1:
     * put the wanted key in entry 1 and expire entry 0's copy of it.
     */
    poison_globals();
    HINT_$INIT_CACHE();
    TIME_$CLOCKH = 1000;
    HINT_$ADD_CACHE(&key0, &r0);
    TIME_$CLOCKH = 1000 + HINT_CACHE_TIMEOUT;
    HINT_$ADD_CACHE(&key0, &r1);      /* same key, fresh, in entry 1 */

    out = 0xFF;
    HINT_$LOOKUP_CACHE(&key0, &out);
    ASSERT_EQ(0x22, out);
    ASSERT_EQ(1000 + HINT_CACHE_TIMEOUT, HINT_$CACHE[1].timestamp);
}

/*
 * The lookup bound: a key planted at globals+0x14 (which a third iteration
 * would read as entry 2's key) must never be found.
 */
TEST(lookup_cache_scan_runs_exactly_twice)
{
    uint32_t key = 0x00088888;
    uint8_t out = 0xFF;
    uint8_t *raw = (uint8_t *)&HINT_$GLOBALS_BLOCK;
    uint32_t planted = key;

    poison_globals();
    HINT_$INIT_CACHE();
    TIME_$CLOCKH = 2000;

    /* Where entry 2's uid_low_masked would sit: globals + 24 + 8 = 0x20. */
    memcpy(raw + sizeof(hint_cache_entry_t) * HINT_CACHE_SIZE + 8,
           &planted, sizeof(planted));
    /* And a fresh stamp where entry 2's timestamp would sit (globals+0x18). */
    memset(raw + sizeof(hint_cache_entry_t) * HINT_CACHE_SIZE, 0, 4);

    HINT_$LOOKUP_CACHE(&key, &out);

    /* Two empty entries, no match: the result byte is cleared. */
    ASSERT_EQ(0, out);
}

int main(void)
{
    printf("HINT local cache tests\n");

    RUN_TEST(cache_layout);
    RUN_TEST(init_cache_clears_two_entries);
    RUN_TEST(add_cache_fills_the_first_free_entry);
    RUN_TEST(add_cache_fills_the_second_free_entry);
    RUN_TEST(add_cache_scan_runs_exactly_twice);
    RUN_TEST(add_cache_round_robin_wraps_at_two);
    RUN_TEST(lookup_cache_hit_refreshes);
    RUN_TEST(lookup_cache_expired_entry_keeps_scanning);
    RUN_TEST(lookup_cache_scan_runs_exactly_twice);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
