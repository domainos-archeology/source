/*
 * hint/test/test_hintfile.c - Unit tests for the two hint-file walkers,
 * HINT_$GET_HINTS (0x00E49966) and HINT_$add_internal (0x00E49A2C).
 *
 * Both walk their bucket's three slots ASCENDING and HINT_$GET_HINTS walks a
 * slot's three address pairs ascending too, so the tests plant distinguishable
 * values in every slot and pair and check which one wins and in what order.
 * They also pin the layout the "+0x10 biased pointer" addressing implies:
 * slot stride 0x1C, bucket stride 0x54, buckets starting at hintfile+0x0C.
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

hint_globals_t HINT_$GLOBALS_BLOCK;
hint_file_t *HINT_$HINTFILE_PTR;
uint32_t NODE_$ME;
uint32_t ROUTE_$PORT;

#define HINT_$BUCKET_INDEX (HINT_$GLOBALS->bucket_index)

static hint_file_t test_hintfile;

#include "../get_hints.c"
#include "../add_internal.c"

/* ==========================================================================
 * Harness
 * ========================================================================== */

/* The node this test uses as "the local node" in the trailing entry. */
#define TEST_NODE_ME 0x000AAAAAu

static void reset_state(void)
{
    memset(&test_hintfile, 0, sizeof(test_hintfile));
    memset(&HINT_$GLOBALS_BLOCK, 0, sizeof(HINT_$GLOBALS_BLOCK));
    HINT_$HINTFILE_PTR = &test_hintfile;
    HINT_$BUCKET_INDEX = 1;
    NODE_$ME = TEST_NODE_ME;
    ROUTE_$PORT = 0;
}

static uid_t make_uid(uint32_t high, uint32_t low)
{
    uid_t u;

    u.high = high;
    u.low = low;
    return u;
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

/*
 * The strides the biased addressing implies: the scan holds slot i at
 * bucket_base + 0x1C + 0x1C*i and reads the key at -0x10 from it, which lands
 * on hintfile + 0x54*b + 0x0C - so buckets start at +0x0C, right after the
 * 12-byte header.
 */
TEST(hintfile_layout)
{
    ASSERT_EQ(0x1C, sizeof(hint_slot_t));
    ASSERT_EQ(0x54, sizeof(hint_bucket_t));
    ASSERT_EQ(0x0C, __builtin_offsetof(hint_file_t, buckets));
    /*
     * source-nrfl: 65 buckets, not 64.  HINT_$clear_hintfile's outer loop is
     * `moveq #0x40,D0` (0x00E311E0) + `dbf D0w` (0x00E3121E) = 0x41
     * iterations at a 0x54 stride, so the record has to reach
     * 0x0C + 65*0x54 = 0x1560 for that last iteration to be in bounds, even
     * though `andi.w #0x3f` (0x00E49A5E) never selects bucket 64.
     */
    ASSERT_EQ(65, HINT_HASH_SLOTS);
    ASSERT_EQ(64, HINT_HASH_SIZE);
    ASSERT_EQ(0x1560, sizeof(hint_file_t));
    ASSERT_EQ(0x0C + 65 * 0x54, sizeof(hint_file_t));

    ASSERT_EQ(0x00, __builtin_offsetof(hint_slot_t, uid_low_masked));
    ASSERT_EQ(0x04, __builtin_offsetof(hint_slot_t, addrs[0]));
    ASSERT_EQ(0x0C, __builtin_offsetof(hint_slot_t, addrs[1]));
    ASSERT_EQ(0x14, __builtin_offsetof(hint_slot_t, addrs[2]));

    /* bucket 1 slot 0's key really is hintfile + 0x54 + 0x0C */
    ASSERT_EQ(0x54 + 0x0C,
              (uintptr_t)&test_hintfile.buckets[1].slots[0].uid_low_masked -
                  (uintptr_t)&test_hintfile);
}

/* ==========================================================================
 * HINT_$GET_HINTS
 * ========================================================================== */

/* 0x00E49984 / 0x00E4998C: both early exits still emit the local-node entry. */
TEST(get_hints_early_exits)
{
    uint32_t out[16];
    uid_t u;
    int16_t n;

    reset_state();
    memset(out, 0xEE, sizeof(out));
    u = make_uid(0x11112222, 0x33300000);      /* key == 0 */
    n = HINT_$GET_HINTS(&u, out);
    ASSERT_EQ(1, n);
    ASSERT_EQ(0, out[0]);
    ASSERT_EQ(TEST_NODE_ME, out[1]);

    reset_state();
    HINT_$HINTFILE_PTR = NULL;
    memset(out, 0xEE, sizeof(out));
    u = make_uid(0, 0x00012345);
    n = HINT_$GET_HINTS(&u, out);
    ASSERT_EQ(2, n);                           /* self entry, then the node */
    ASSERT_EQ(0, out[0]);
    ASSERT_EQ(0x00012345, out[1]);
    ASSERT_EQ(0, out[2]);
    ASSERT_EQ(TEST_NODE_ME, out[3]);
}

/*
 * The scan direction.  Plant the key in slot 0 with one set of addresses and
 * in slot 2 with another; an ascending scan (lea (0x1c,A0),A0 at 0x00E499E6)
 * takes slot 0.
 */
TEST(get_hints_scan_ascends)
{
    uint32_t out[16];
    uid_t u;
    hint_bucket_t *b;
    int16_t n;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];

    b->slots[0].uid_low_masked = 0x00012345;
    b->slots[0].addrs[0].flags = 0xAA;
    b->slots[0].addrs[0].node_id = 0x1111;

    b->slots[2].uid_low_masked = 0x00012345;
    b->slots[2].addrs[0].flags = 0xCC;
    b->slots[2].addrs[0].node_id = 0x3333;

    memset(out, 0xEE, sizeof(out));
    n = HINT_$GET_HINTS(&u, out);

    /* Slot 0 won. */
    ASSERT_EQ(0xAA, out[0]);
    ASSERT_EQ(0x1111, out[1]);
    /* One hint + the self entry + the local node. */
    ASSERT_EQ(3, n);
    ASSERT_EQ(0, out[2]);
    ASSERT_EQ(0x00012345, out[3]);
    ASSERT_EQ(0, out[4]);
    ASSERT_EQ(TEST_NODE_ME, out[5]);
}

/*
 * The address direction.  Three pairs in one slot come out in index order
 * (addq.l #0x8,A2 at 0x00E499DE steps forward).
 */
TEST(get_hints_addresses_ascend)
{
    uint32_t out[16];
    uid_t u;
    hint_bucket_t *b;
    int16_t n;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];

    b->slots[0].uid_low_masked = 0x00012345;
    b->slots[0].addrs[0].flags = 0x0A; b->slots[0].addrs[0].node_id = 0x1111;
    b->slots[0].addrs[1].flags = 0x0B; b->slots[0].addrs[1].node_id = 0x2222;
    b->slots[0].addrs[2].flags = 0x0C; b->slots[0].addrs[2].node_id = 0x3333;

    memset(out, 0xEE, sizeof(out));
    n = HINT_$GET_HINTS(&u, out);

    ASSERT_EQ(0x0A, out[0]); ASSERT_EQ(0x1111, out[1]);
    ASSERT_EQ(0x0B, out[2]); ASSERT_EQ(0x2222, out[3]);
    ASSERT_EQ(0x0C, out[4]); ASSERT_EQ(0x3333, out[5]);
    /* 3 hints + self + node */
    ASSERT_EQ(5, n);
    ASSERT_EQ(0x00012345, out[7]);
    ASSERT_EQ(TEST_NODE_ME, out[9]);
}

/* 0x00E499BE: a zero node id ends the copy at once. */
TEST(get_hints_stops_at_an_empty_pair)
{
    uint32_t out[16];
    uid_t u;
    hint_bucket_t *b;
    int16_t n;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];

    b->slots[0].uid_low_masked = 0x00012345;
    b->slots[0].addrs[0].flags = 0x0A; b->slots[0].addrs[0].node_id = 0x1111;
    /* addrs[1] left empty */
    b->slots[0].addrs[2].flags = 0x0C; b->slots[0].addrs[2].node_id = 0x3333;

    memset(out, 0xEE, sizeof(out));
    n = HINT_$GET_HINTS(&u, out);

    ASSERT_EQ(0x1111, out[1]);
    /* addrs[2] was never reached. */
    ASSERT_EQ(3, n);
    ASSERT_EQ(0x00012345, out[3]);
    ASSERT_EQ(TEST_NODE_ME, out[5]);
}

/*
 * 0x00E499D4-0x00E499D8: a hint that already names the object's own node
 * suppresses the extra self entry.
 */
TEST(get_hints_self_hint_suppresses_the_extra_entry)
{
    uint32_t out[16];
    uid_t u;
    hint_bucket_t *b;
    int16_t n;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];

    b->slots[0].uid_low_masked = 0x00012345;
    b->slots[0].addrs[0].flags = 0x0A;
    b->slots[0].addrs[0].node_id = 0x00012345;   /* == the key */

    memset(out, 0xEE, sizeof(out));
    n = HINT_$GET_HINTS(&u, out);

    ASSERT_EQ(2, n);
    ASSERT_EQ(0x00012345, out[1]);
    ASSERT_EQ(0, out[2]);
    ASSERT_EQ(TEST_NODE_ME, out[3]);
}

/* 0x00E499F2 "cmpi.l #0x4,D0 / bls" - an UNSIGNED compare, so 1..4 get none. */
TEST(get_hints_small_keys_get_no_self_entry)
{
    uint32_t out[16];
    uid_t u;
    int16_t n;

    reset_state();
    u = make_uid(0, 4);
    memset(out, 0xEE, sizeof(out));
    n = HINT_$GET_HINTS(&u, out);
    ASSERT_EQ(1, n);
    ASSERT_EQ(TEST_NODE_ME, out[1]);

    reset_state();
    u = make_uid(0, 5);
    memset(out, 0xEE, sizeof(out));
    n = HINT_$GET_HINTS(&u, out);
    ASSERT_EQ(2, n);
    ASSERT_EQ(5, out[1]);
    ASSERT_EQ(TEST_NODE_ME, out[3]);
}

/* ==========================================================================
 * HINT_$add_internal
 * ========================================================================== */

/* 0x00E49A3C / 0x00E49A44: both early exits. */
TEST(add_internal_early_exits)
{
    hint_addr_t a;
    uid_t u;

    reset_state();
    u = make_uid(0, 0x00012345);
    a.flags = 7;
    a.node_id = 0;                      /* nothing to add */
    HINT_$add_internal(&u, &a);
    ASSERT_EQ(0, test_hintfile.buckets[0x00012345 & HINT_HASH_MASK]
                     .slots[0].uid_low_masked);

    reset_state();
    HINT_$HINTFILE_PTR = NULL;
    a.node_id = 0x1111;
    HINT_$add_internal(&u, &a);
    ASSERT_EQ(0, test_hintfile.buckets[0x00012345 & HINT_HASH_MASK]
                     .slots[0].uid_low_masked);
}

/*
 * The free-slot scan ascends and remembers the FIRST empty slot
 * (0x00E49ACE-0x00E49AD8), so a fresh bucket fills slot 0.
 */
TEST(add_internal_uses_the_first_free_slot)
{
    hint_addr_t a;
    uid_t u;
    hint_bucket_t *b;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];
    a.flags = 7;
    a.node_id = 0x1111;

    HINT_$add_internal(&u, &a);

    ASSERT_EQ(0x00012345, b->slots[0].uid_low_masked);
    ASSERT_EQ(7, b->slots[0].addrs[0].flags);
    ASSERT_EQ(0x1111, b->slots[0].addrs[0].node_id);
    /* The object's own node becomes the second hint, same flags. */
    ASSERT_EQ(7, b->slots[0].addrs[1].flags);
    ASSERT_EQ(0x00012345, b->slots[0].addrs[1].node_id);
    ASSERT_EQ(0, b->slots[0].addrs[2].node_id);
    /* Slots 1 and 2 untouched. */
    ASSERT_EQ(0, b->slots[1].uid_low_masked);
    ASSERT_EQ(0, b->slots[2].uid_low_masked);
}

/* With slot 0 taken by another key the scan lands on slot 1, not slot 2. */
TEST(add_internal_free_slot_scan_ascends)
{
    hint_addr_t a;
    uid_t u;
    hint_bucket_t *b;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];
    b->slots[0].uid_low_masked = 0x000FFFFF;     /* occupied by someone else */

    a.flags = 7;
    a.node_id = 0x1111;
    HINT_$add_internal(&u, &a);

    ASSERT_EQ(0x00012345, b->slots[1].uid_low_masked);
    ASSERT_EQ(0, b->slots[2].uid_low_masked);
}

/* 0x00E49A90-0x00E49A9E: the pair is already first, so only the flags move. */
TEST(add_internal_refreshes_the_leading_pair)
{
    hint_addr_t a;
    uid_t u;
    hint_bucket_t *b;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];

    b->slots[0].uid_low_masked = 0x00012345;
    b->slots[0].addrs[0].flags = 1; b->slots[0].addrs[0].node_id = 0x1111;
    b->slots[0].addrs[1].flags = 2; b->slots[0].addrs[1].node_id = 0x2222;
    b->slots[0].addrs[2].flags = 3; b->slots[0].addrs[2].node_id = 0x3333;

    a.flags = 9;
    a.node_id = 0x1111;
    HINT_$add_internal(&u, &a);

    ASSERT_EQ(9, b->slots[0].addrs[0].flags);
    ASSERT_EQ(0x1111, b->slots[0].addrs[0].node_id);
    /* Nothing else moved. */
    ASSERT_EQ(2, b->slots[0].addrs[1].flags);
    ASSERT_EQ(0x2222, b->slots[0].addrs[1].node_id);
    ASSERT_EQ(3, b->slots[0].addrs[2].flags);
    ASSERT_EQ(0x3333, b->slots[0].addrs[2].node_id);
}

/*
 * The condition this test pins.  "cmp.l (0x4,A2),D6 / beq.b 0x00E49AB6" at
 * 0x00E49AA4 SKIPS the addrs[1] -> addrs[2] copy when addrs[1] already names
 * the node being promoted; the shift happens only when they DIFFER.
 */
TEST(add_internal_shifts_only_when_the_second_pair_differs)
{
    hint_addr_t a;
    uid_t u;
    hint_bucket_t *b;

    /* addrs[1] names a DIFFERENT node: it is pushed down to addrs[2]. */
    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];
    b->slots[0].uid_low_masked = 0x00012345;
    b->slots[0].addrs[0].flags = 1; b->slots[0].addrs[0].node_id = 0x1111;
    b->slots[0].addrs[1].flags = 2; b->slots[0].addrs[1].node_id = 0x2222;
    b->slots[0].addrs[2].flags = 3; b->slots[0].addrs[2].node_id = 0x3333;

    a.flags = 9;
    a.node_id = 0x4444;                 /* != addrs[1].node_id */
    HINT_$add_internal(&u, &a);

    ASSERT_EQ(9, b->slots[0].addrs[0].flags);
    ASSERT_EQ(0x4444, b->slots[0].addrs[0].node_id);
    ASSERT_EQ(1, b->slots[0].addrs[1].flags);
    ASSERT_EQ(0x1111, b->slots[0].addrs[1].node_id);
    /* the old addrs[1] was preserved into addrs[2] */
    ASSERT_EQ(2, b->slots[0].addrs[2].flags);
    ASSERT_EQ(0x2222, b->slots[0].addrs[2].node_id);

    /* addrs[1] names the SAME node: no copy, so addrs[2] keeps its value. */
    reset_state();
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];
    b->slots[0].uid_low_masked = 0x00012345;
    b->slots[0].addrs[0].flags = 1; b->slots[0].addrs[0].node_id = 0x1111;
    b->slots[0].addrs[1].flags = 2; b->slots[0].addrs[1].node_id = 0x2222;
    b->slots[0].addrs[2].flags = 3; b->slots[0].addrs[2].node_id = 0x3333;

    a.flags = 9;
    a.node_id = 0x2222;                 /* == addrs[1].node_id */
    HINT_$add_internal(&u, &a);

    ASSERT_EQ(0x2222, b->slots[0].addrs[0].node_id);
    ASSERT_EQ(0x1111, b->slots[0].addrs[1].node_id);
    /* untouched - the duplicate was dropped rather than kept twice */
    ASSERT_EQ(3, b->slots[0].addrs[2].flags);
    ASSERT_EQ(0x3333, b->slots[0].addrs[2].node_id);
}

/*
 * 0x00E49AE4-0x00E49AF4: a hint that only says "the object is on its own
 * node" is dropped when the flags word is zero or is this node's port.
 */
TEST(add_internal_rejects_empty_self_hints)
{
    hint_addr_t a;
    uid_t u;
    hint_bucket_t *b;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];
    a.flags = 0;
    a.node_id = 0x00012345;
    HINT_$add_internal(&u, &a);
    ASSERT_EQ(0, b->slots[0].uid_low_masked);

    reset_state();
    ROUTE_$PORT = 0x77;
    a.flags = 0x77;
    a.node_id = 0x00012345;
    HINT_$add_internal(&u, &a);
    ASSERT_EQ(0, b->slots[0].uid_low_masked);

    /* A different flags word is kept, and clears the second pair. */
    reset_state();
    ROUTE_$PORT = 0x77;
    a.flags = 0x88;
    a.node_id = 0x00012345;
    HINT_$add_internal(&u, &a);
    ASSERT_EQ(0x00012345, b->slots[0].uid_low_masked);
    ASSERT_EQ(0x88, b->slots[0].addrs[0].flags);
    ASSERT_EQ(0, b->slots[0].addrs[1].flags);
    ASSERT_EQ(0, b->slots[0].addrs[1].node_id);
}

/*
 * 0x00E49AF6-0x00E49B10: with all three slots taken by other keys the
 * round-robin index picks the victim and cycles 1,2,3,1.
 */
TEST(add_internal_round_robin_victim)
{
    hint_addr_t a;
    uid_t u;
    hint_bucket_t *b;
    int k;

    reset_state();
    u = make_uid(0, 0x00012345);
    b = &test_hintfile.buckets[0x00012345 & HINT_HASH_MASK];
    b->slots[0].uid_low_masked = 0x000F0001;
    b->slots[1].uid_low_masked = 0x000F0002;
    b->slots[2].uid_low_masked = 0x000F0003;

    a.flags = 7;
    a.node_id = 0x1111;

    for (k = 0; k < 4; k++) {
        /* Re-occupy so every pass takes the round-robin path. */
        b->slots[0].uid_low_masked = 0x000F0001;
        b->slots[1].uid_low_masked = 0x000F0002;
        b->slots[2].uid_low_masked = 0x000F0003;
        HINT_$BUCKET_INDEX = (int16_t)(k % 3 + 1);

        HINT_$add_internal(&u, &a);

        ASSERT_EQ(0x00012345, b->slots[k % 3].uid_low_masked);
    }

    /* The index wraps from 3 back to 1. */
    HINT_$BUCKET_INDEX = 3;
    b->slots[0].uid_low_masked = 0x000F0001;
    b->slots[1].uid_low_masked = 0x000F0002;
    b->slots[2].uid_low_masked = 0x000F0003;
    HINT_$add_internal(&u, &a);
    ASSERT_EQ(1, HINT_$BUCKET_INDEX);
}

/*
 * 0x00E49B40-0x00E49B58: the reverse hint.  The recursive call keys off
 * (garbage & 0xFFF00000) | node_id, and the callee masks with 0xFFFFF, so the
 * entry lands under the node id.  The stale high bits cannot change that.
 */
TEST(add_internal_adds_the_reverse_hint)
{
    hint_addr_t a;
    uid_t u;
    hint_bucket_t *rev;

    reset_state();
    u = make_uid(0, 0x00012345);
    a.flags = 7;
    a.node_id = 0x00054321;

    HINT_$add_internal(&u, &a);

    /* The reverse entry is keyed by the node id, in ITS bucket. */
    rev = &test_hintfile.buckets[0x00054321 & HINT_HASH_MASK];
    ASSERT_EQ(0x00054321, rev->slots[0].uid_low_masked);
    ASSERT_EQ(7, rev->slots[0].addrs[0].flags);
    ASSERT_EQ(0x00054321, rev->slots[0].addrs[0].node_id);
    /* Its key equals its node, so the recursion stopped there. */
    ASSERT_EQ(0, rev->slots[0].addrs[1].node_id);
}

/* A round trip: what add_internal wrote, get_hints reads back in order. */
TEST(add_then_get_round_trip)
{
    hint_addr_t a;
    uid_t u;
    uint32_t out[16];
    int16_t n;

    reset_state();
    u = make_uid(0, 0x00012345);

    a.flags = 7;  a.node_id = 0x00011111;
    HINT_$add_internal(&u, &a);
    a.flags = 8;  a.node_id = 0x00022222;
    HINT_$add_internal(&u, &a);

    memset(out, 0xEE, sizeof(out));
    n = HINT_$GET_HINTS(&u, out);

    /* Most recent first. */
    ASSERT_EQ(8, out[0]);          ASSERT_EQ(0x00022222, out[1]);
    ASSERT_EQ(7, out[2]);          ASSERT_EQ(0x00011111, out[3]);
    ASSERT_EQ(7, out[4]);          ASSERT_EQ(0x00012345, out[5]);
    /* The third hint already names the object's node, so no extra entry. */
    ASSERT_EQ(4, n);
    ASSERT_EQ(0, out[6]);
    ASSERT_EQ(TEST_NODE_ME, out[7]);
}

int main(void)
{
    printf("HINT hint-file tests\n");

    RUN_TEST(hintfile_layout);
    RUN_TEST(get_hints_early_exits);
    RUN_TEST(get_hints_scan_ascends);
    RUN_TEST(get_hints_addresses_ascend);
    RUN_TEST(get_hints_stops_at_an_empty_pair);
    RUN_TEST(get_hints_self_hint_suppresses_the_extra_entry);
    RUN_TEST(get_hints_small_keys_get_no_self_entry);
    RUN_TEST(add_internal_early_exits);
    RUN_TEST(add_internal_uses_the_first_free_slot);
    RUN_TEST(add_internal_free_slot_scan_ascends);
    RUN_TEST(add_internal_refreshes_the_leading_pair);
    RUN_TEST(add_internal_shifts_only_when_the_second_pair_differs);
    RUN_TEST(add_internal_rejects_empty_self_hints);
    RUN_TEST(add_internal_round_robin_victim);
    RUN_TEST(add_internal_adds_the_reverse_hint);
    RUN_TEST(add_then_get_round_trip);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
