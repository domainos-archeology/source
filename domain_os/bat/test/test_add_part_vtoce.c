/*
 * bat/test/test_add_part_vtoce.c - BAT_$ADD_PART_VTOCE (0x00E3AE2E).
 *
 * Bead source-jxtg.  The routine swaps the caller's block into the
 * partition's chain head and hands back the old one:
 *
 *   00e3ae86  move.l  #0xffffff,D2
 *   00e3ae94  and.l   (-0x204,A0),D2          ; result = OLD & 0x00FFFFFF
 *   00e3ae98  andi.l  #-0x1000000,(-0x204,A0) ; keep the status byte
 *   00e3aea0  move.l  (0xa,A6),D1             ; the NEW block, unmasked
 *   00e3aea4  or.l    D1,(-0x204,A0)
 *
 * Only the RESULT is masked to 24 bits.  The store ORs the caller's whole
 * longword over the chain word, so a block with bits above 23 set writes
 * through into the partition status byte - the same unmasked store
 * BAT_$ALLOC_VTOCE makes at 0x00E3B08E.  The tree used to write three
 * masked bytes, which could not reproduce that.
 *
 * The partition index selection is also pinned: 0x00E3AE62-0x00E3AE6A
 * compares the (zero-extended) partition_start_offset WORD against the
 * block and takes index 0 when the block is below it, otherwise
 * M$DIS$LLL((block - start), partition_size).
 */

#include "bat/bat_internal.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Mocks
 * ============================================================================ */

static int lock_calls;
static int unlock_calls;
static int16_t last_lock_id;

void ML_$LOCK(int16_t id)   { lock_calls++;   last_lock_id = id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; }

/*
 * 0x00E0ACD8 M$DIS$LLL: the runtime's signed long divide, used here only
 * for (block - partition_start_offset) / partition_size.
 */
long M$DIS$LLL(long dividend, long divisor)
{
    return dividend / divisor;
}

bat_$volume_t bat_$volumes[BAT_MAX_VOLUMES];

#include "../add_part_vtoce.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

#define VOL 2

static void reset(void)
{
    memset(bat_$volumes, 0, sizeof(bat_$volumes));
    bat_$volumes[VOL].partition_start_offset = 0x100;
    bat_$volumes[VOL].partition_size = 0x1000;
    bat_$volumes[VOL].num_partitions = 4;
    lock_calls = unlock_calls = 0;
    last_lock_id = -1;
}

static uint32_t chain(int part)
{
    return BAT_PART_CHAIN_LONG(&bat_$volumes[VOL].partitions[part]);
}

static void set_chain(int part, uint32_t value)
{
    BAT_SET_PART_CHAIN_LONG(&bat_$volumes[VOL].partitions[part], value);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* 0x00E3AE3C / 0x00E3AEAA: ML_$LOCK(0x11) around the whole body. */
TEST(takes_the_bat_lock)
{
    status_$t status = 0x11223344;

    reset();
    (void)BAT_$ADD_PART_VTOCE(VOL, 0x000123, &status);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(ML_LOCK_BAT, last_lock_id);
    ASSERT_EQ(status_$ok, status);      /* 0x00E3AE52 `clr.l (A0)` */
}

/*
 * 0x00E3AE62-0x00E3AE6E: a block below partition_start_offset selects
 * partition 0 with no division at all.
 */
TEST(block_below_the_start_offset_uses_partition_0)
{
    status_$t status;

    reset();
    set_chain(0, 0x01000000u);          /* status byte 1, empty chain */
    set_chain(1, 0x02AAAAAAu);

    ASSERT_EQ(0u, BAT_$ADD_PART_VTOCE(VOL, 0x0000FF, &status));
    ASSERT_EQ(0x010000FFu, chain(0));
    ASSERT_EQ(0x02AAAAAAu, chain(1));   /* untouched */
}

/* 0x00E3AE70-0x00E3AE82: otherwise (block - start) / partition_size. */
TEST(block_above_the_start_offset_divides)
{
    status_$t status;

    reset();
    set_chain(2, 0x07000000u);

    /* (0x2100 - 0x100) / 0x1000 == 2 */
    ASSERT_EQ(0u, BAT_$ADD_PART_VTOCE(VOL, 0x002100, &status));
    ASSERT_EQ(0x07002100u, chain(2));
}

/*
 * 0x00E3AE86-0x00E3AE94: the RESULT is the old chain masked to 24 bits, so
 * the status byte never reaches the caller.
 */
TEST(result_is_the_old_block_masked_to_24_bits)
{
    status_$t status;

    reset();
    set_chain(0, 0xAB123456u);

    ASSERT_EQ(0x123456u, BAT_$ADD_PART_VTOCE(VOL, 0x000010, &status));
    ASSERT_EQ(0xAB000010u, chain(0));   /* status byte preserved */
}

/*
 * source-jxtg: 0x00E3AEA0-0x00E3AEA4 ORs the new block UNMASKED.  A block
 * with bits above 23 set therefore corrupts the partition status byte, and
 * the C must reproduce that rather than silently dropping the high byte.
 */
TEST(an_out_of_range_block_writes_through_into_the_status_byte)
{
    status_$t status;

    reset();
    set_chain(1, 0x01000000u);          /* status 1, chain empty */

    /*
     * (0x40001100 - 0x100) / 0x1000 == 0x40001, and 0x00E3AE84
     * `move.w D0w,D1w` keeps only the low word of the quotient, so the
     * index is 1.
     */
    ASSERT_EQ(0u, BAT_$ADD_PART_VTOCE(VOL, 0x40001100u, &status));

    /* 0x01000000 & 0xFF000000 == 0x01000000, OR 0x40001100 -> 0x41001100 */
    ASSERT_EQ(0x41001100u, chain(1));
    ASSERT_EQ(0x41, bat_$volumes[VOL].partitions[1].status);
}

/* The same store, seen from the other side: every status bit already set
 * stays set, because the OR can only add bits. */
TEST(the_unmasked_or_only_adds_status_bits)
{
    status_$t status;

    reset();
    set_chain(0, 0x80000000u);

    /* (0x40000100 - 0x100) / 0x1000 == 0x40000, low word 0 -> partition 0 */
    ASSERT_EQ(0u, BAT_$ADD_PART_VTOCE(VOL, 0x40000100u, &status));
    ASSERT_EQ(0xC0000100u, chain(0));
}

int main(void)
{
    printf("BAT_$ADD_PART_VTOCE tests\n");
    RUN_TEST(takes_the_bat_lock);
    RUN_TEST(block_below_the_start_offset_uses_partition_0);
    RUN_TEST(block_above_the_start_offset_divides);
    RUN_TEST(result_is_the_old_block_masked_to_24_bits);
    RUN_TEST(an_out_of_range_block_writes_through_into_the_status_byte);
    RUN_TEST(the_unmasked_or_only_adds_status_bits);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
