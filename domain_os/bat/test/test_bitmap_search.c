/*
 * bat/test/test_bitmap_search.c - the three BAT bitmap routines.
 *
 * Drives the real bat/allocate.c, bat/free.c and bat/alloc_vtoce.c through
 * a mock DBUF that hands back an in-memory 1024-byte BAT block, so the
 * tests observe exactly the bits and counters the machine code moves.
 *
 * The properties under test are the ones a decompiler gets wrong:
 *
 *   BAT_$ALLOCATE  the two argument WORDS at (0x0e,A6) / (0x10,A6) are
 *                  alloc_count and use_reserved, in that order
 *                  (0x00E3B120 tst.w, 0x00E3B38E cmp.w)
 *   BAT_$ALLOCATE  inside a partition the next chunk starts at the OLD
 *                  chunk's end, with no division (0x00E3B472)
 *   BAT_$FREE      the block array is walked ASCENDING (0x00E3B6C2
 *                  addq.l #0x4 on the cursor)
 *   BAT_$ALLOC_VTOCE  the hint partition survives free_count == threshold
 *                  (0x00E3AF46 cmp.l / 0x00E3AF4A bls)
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

/*
 * status_$t is int32_t and the BAT codes have the top bit set, so compare
 * statuses as 32-bit unsigned rather than widening a negative int.
 */
#define ASSERT_STATUS(expected, actual) \
    ASSERT_EQ((uint32_t)(expected), (uint32_t)(actual))

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

#define BAT_WORDS_PER_BLOCK 0x100
#define MOCK_BAT_BLOCKS     4

/* Four consecutive BAT bitmap blocks, addressed by absolute block number. */
static uint32_t mock_bat[MOCK_BAT_BLOCKS][BAT_WORDS_PER_BLOCK];
static uint32_t mock_bat_base;              /* block number of mock_bat[0] */
static status_$t mock_get_status;
static int       mock_get_calls;
static int       mock_set_calls;
static uint32_t  mock_get_fail_after;       /* fail the Nth GET_BLOCK (1-based) */

/*
 * A single VTOCE block for BAT_$ALLOC_VTOCE.  The mock tells the two block
 * kinds apart by the UID the caller passes: BAT_$ALLOCATE asks for
 * BAT_$UID (0x00E3B2CC pushes 0xE173A4) and BAT_$ALLOC_VTOCE asks for
 * VTOC_$UID (0x00E3B046 pushes 0xE1739C).
 */
static uint8_t   mock_vtoce_block[1024];

uid_t VTOC_$UID;

void ML_$LOCK(int16_t id) { (void)id; }
void ML_$UNLOCK(int16_t id) { (void)id; }

void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint32_t flags, status_$t *status)
{
    uint32_t index;

    (void)vol_idx; (void)block_hint; (void)flags;

    mock_get_calls++;
    if (mock_get_fail_after != 0 && (uint32_t)mock_get_calls >= mock_get_fail_after) {
        *status = bat_$error;
        return NULL;
    }
    *status = mock_get_status;
    if (*status != status_$ok) {
        return NULL;
    }

    if (uid == &VTOC_$UID) {
        return mock_vtoce_block;
    }

    index = (uint32_t)block - mock_bat_base;
    if (index >= MOCK_BAT_BLOCKS) {
        printf("\n    mock: BAT block %ld out of range\n", (long)block);
        tests_failed++;
        index = 0;
    }
    return mock_bat[index];
}

void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    (void)buffer; (void)flags;
    mock_set_calls++;
    *status = status_$ok;
}

long M$DIS$LLL(long dividend, long divisor)
{
    return divisor == 0 ? 0 : dividend / divisor;
}

long M$MIS$LLL(long multiplicand, long multiplier)
{
    return multiplicand * multiplier;
}

long M$MIS$LLW(long multiplicand, short multiplier)
{
    return multiplicand * (long)multiplier;
}

/* Implementation and data under test (included directly) */
#include "../bat_data.c"
#include "../allocate.c"
#include "../free.c"
#include "../alloc_vtoce.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

#define TEST_VOL 1      /* volume indices run 1..6 (0x00E3BA1A / 0x00E3BA1E) */

#define FIRST_DATA_BLOCK 0x100u
#define BAT_BLOCK_START  0x010u
#define TOTAL_BLOCKS     0x800u

static bat_$volume_t *the_vol(void) { return &bat_$volumes[TEST_VOL]; }

/* Mark relative blocks [first, last] free (bit set) in the mock bitmap. */
static void mark_free(uint32_t first, uint32_t last)
{
    uint32_t b;
    for (b = first; b <= last; b++) {
        mock_bat[b >> 13][(b >> 5) & 0xFF] |= 1UL << (b & 0x1F);
    }
}

static int is_free(uint32_t rel)
{
    return (mock_bat[rel >> 13][(rel >> 5) & 0xFF] & (1UL << (rel & 0x1F))) != 0;
}

static void reset_world(void)
{
    bat_$volume_t *vol;

    memset(mock_bat, 0, sizeof(mock_bat));
    memset(mock_vtoce_block, 0, sizeof(mock_vtoce_block));
    memset(bat_$volumes, 0, sizeof(bat_$volumes));
    memset(bat_$mounted, 0, sizeof(bat_$mounted));
    memset(bat_$volume_flags, 0, sizeof(bat_$volume_flags));

    mock_bat_base = BAT_BLOCK_START;
    mock_get_status = status_$ok;
    mock_get_calls = 0;
    mock_set_calls = 0;
    mock_get_fail_after = 0;

    bat_$cached_buffer = NULL;
    bat_$cached_block = 0;
    bat_$cached_vol = 0;
    bat_$cached_dirty = 0;

    bat_$mounted[TEST_VOL] = (int8_t)0xFF;
    bat_$volume_flags[TEST_VOL] = (int8_t)-1;   /* new format: no 0xB cushion */

    vol = the_vol();
    vol->total_blocks      = TOTAL_BLOCKS;
    vol->free_blocks       = 0x400;
    vol->reserved_blocks   = 0x40;
    vol->bat_block_start   = BAT_BLOCK_START;
    vol->first_data_block  = FIRST_DATA_BLOCK;
    vol->step_blocks       = 0;                 /* the +0x12 longword is */
    vol->bat_step          = 1;                 /* step_blocks:bat_step = 1 */
    vol->num_partitions    = 4;
    vol->partition_start_offset = (uint16_t)FIRST_DATA_BLOCK;
    vol->partition_size    = 0x200;
    vol->alloc_chunk_size  = 0x20;
    vol->alloc_chunk_offset = 0;
    vol->partitions[0].free_count = 0x100;
    vol->partitions[1].free_count = 0x100;
    vol->partitions[2].free_count = 0x100;
    vol->partitions[3].free_count = 0x100;
}

/* ============================================================================
 * BAT_$ALLOCATE - the two argument words
 * ============================================================================ */

/*
 * (0x0e,A6) is alloc_count.  Three blocks requested must produce three
 * output entries and three cleared bits (0x00E3B38E cmp.w (0xe,A6),D1w).
 */
TEST(allocate_count_comes_from_the_first_word)
{
    status_$t status = 0x7F;
    uint32_t out[8];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    mark_free(0, 0x1F);
    memset(out, 0xEE, sizeof(out));

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 3, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + 0, out[0]);
    ASSERT_EQ(FIRST_DATA_BLOCK + 1, out[1]);
    ASSERT_EQ(FIRST_DATA_BLOCK + 2, out[2]);
    ASSERT_EQ(0xEEEEEEEE, out[3]);

    ASSERT_EQ(0, is_free(0));
    ASSERT_EQ(0, is_free(1));
    ASSERT_EQ(0, is_free(2));
    ASSERT_EQ(1, is_free(3));

    /* 0x00E3B4F4: the free pool shrank by the count, the reserved pool did not. */
    ASSERT_EQ(0x400 - 3, vol->free_blocks);
    ASSERT_EQ(0x40, vol->reserved_blocks);
}

/*
 * (0x10,A6) is use_reserved: non-zero draws on reserved_blocks both for the
 * capacity test at 0x00E3B15C and for the decrement at 0x00E3B4FC.
 */
TEST(allocate_reserved_flag_comes_from_the_second_word)
{
    status_$t status = 0x7F;
    uint32_t out[8];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    mark_free(0, 0x1F);

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 2, 1, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0x400, vol->free_blocks);
    ASSERT_EQ(0x40 - 2, vol->reserved_blocks);
}

/*
 * 0x00E3B15C compares alloc_count against reserved_blocks ALONE when
 * use_reserved is set, even though the free pool is enormous.
 */
TEST(allocate_reserved_pool_capacity_is_checked_alone)
{
    status_$t status = status_$ok;
    uint32_t out[8];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->reserved_blocks = 2;
    mark_free(0, 0x1F);

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 3, 1, out, &status);
    ASSERT_STATUS(status_$disk_is_full, status);
    ASSERT_EQ(2, vol->reserved_blocks);

    /* Exactly the pool size is allowed: the test is `ble`, not `blt`. */
    status = 0x7F;
    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 2, 1, out, &status);
    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0, vol->reserved_blocks);
}

/*
 * A swapped pair would read count 0 from the high half and flag 1 from the
 * low half of the caller's `move.l #0x10000`.  Passing (1, 0) must allocate
 * one block from the FREE pool.
 */
TEST(allocate_single_block_from_free_pool)
{
    status_$t status = 0x7F;
    uint32_t out[4];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    mark_free(0, 0x1F);
    memset(out, 0xEE, sizeof(out));

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 1, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK, out[0]);
    ASSERT_EQ(0xEEEEEEEE, out[1]);
    ASSERT_EQ(0x400 - 1, vol->free_blocks);
    ASSERT_EQ(0x40, vol->reserved_blocks);
}

/*
 * 0x00E3B126..0x00E3B148: an OLD-format volume keeps a 0xB-block cushion,
 * a new-format one does not.  With free_blocks == 0x10 a request for 0x10
 * succeeds on a new-format volume and fails on an old-format one.
 */
TEST(allocate_old_format_keeps_the_0xb_cushion)
{
    status_$t status = 0x7F;
    uint32_t out[0x20];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->free_blocks = 0x10;
    mark_free(0, 0x3F);

    bat_$volume_flags[TEST_VOL] = 0;            /* old format */
    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 0x10, 0, out, &status);
    ASSERT_STATUS(status_$disk_is_full, status);

    reset_world();
    vol = the_vol();
    vol->free_blocks = 0x10;
    mark_free(0, 0x3F);

    status = 0x7F;
    bat_$volume_flags[TEST_VOL] = (int8_t)-1;   /* new format */
    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 0x10, 0, out, &status);
    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0, vol->free_blocks);
}

/* 0x00E3B0FE: an unmounted volume is refused before anything else. */
TEST(allocate_rejects_unmounted_volume)
{
    status_$t status = status_$ok;
    uint32_t out[4];

    reset_world();
    bat_$mounted[TEST_VOL] = 0;

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 1, 0, out, &status);
    ASSERT_STATUS(bat_$not_mounted, status);
    ASSERT_EQ(0, mock_get_calls);
}

/* ============================================================================
 * BAT_$ALLOCATE - the chunk-advance rule
 * ============================================================================ */

/*
 * 0x00E3B472 `move.l (-0x18,A6),D6`: while the search is still inside the
 * partition and inside the volume, the next chunk begins at the OLD chunk's
 * END and runs one alloc_chunk_size further -- no division, and chunk_end is
 * recomputed from that start (0x00E3B476).
 *
 * With alloc_chunk_size 0x20 and the only free block at relative 0x35, the
 * search starts in chunk [0x20,0x40) (the hint is 0x20), runs off its end,
 * rescans it once, and then must step to [0x40,0x60), [0x60,0x80) ... and
 * eventually wrap.  A model that recomputed the chunk by dividing would
 * land back on the same chunk forever.
 */
TEST(allocate_next_chunk_starts_at_old_chunk_end)
{
    status_$t status = 0x7F;
    uint32_t out[4];

    reset_world();
    /* One free block, in the chunk AFTER the one the hint selects. */
    mark_free(0x45, 0x45);
    memset(out, 0xEE, sizeof(out));

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK + 0x20, 1, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + 0x45, out[0]);
    ASSERT_EQ(0, is_free(0x45));
}

/*
 * The same rule several chunks along: the only free block sits well past the
 * hint's chunk, so the walk has to advance chunk by chunk to reach it.
 */
TEST(allocate_walks_forward_chunk_by_chunk)
{
    status_$t status = 0x7F;
    uint32_t out[4];

    reset_world();
    mark_free(0x123, 0x123);

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 1, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + 0x123, out[0]);
}

/*
 * Crossing a BAT bitmap block: 0x00E3B4CA `cmpi.w #0x100,D4w` bumps
 * bat_block and restarts the word index.  Relative block 0x2001 lives in the
 * SECOND mock block (0x2000 bits per block).
 */
TEST(allocate_crosses_a_bat_block_boundary)
{
    status_$t status = 0x7F;
    uint32_t out[4];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    /* One partition covering the whole volume so the walk is unimpeded. */
    vol->total_blocks = 0x4000;
    vol->num_partitions = 1;
    vol->partition_size = 0x4000;
    vol->partitions[0].free_count = 0x100;
    mark_free(0x2001, 0x2001);

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK + 0x2000, 1, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + 0x2001, out[0]);
    /* The second mock block really was fetched. */
    ASSERT_EQ(BAT_BLOCK_START + 1, bat_$cached_block);
}

/*
 * 0x00E3B18C / 0x00E3B396 read the (step_blocks, bat_step) pair at +0x12 as
 * ONE longword.  With the pair spelling 4, the first free block found is
 * skipped until the counter runs down, so an all-free chunk yields blocks
 * spaced by the stride rather than block 0.
 */
TEST(allocate_stride_comes_from_the_step_longword)
{
    status_$t status = 0x7F;
    uint32_t out[4];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->step_blocks = 0;
    vol->bat_step = 4;              /* the +0x12 longword is 4 */
    mark_free(0, 0x1F);
    memset(out, 0xEE, sizeof(out));

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 2, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    /* step_remaining starts at 4-1 and must reach 0 before a bit is taken. */
    ASSERT_EQ(FIRST_DATA_BLOCK + 3, out[0]);
    ASSERT_EQ(FIRST_DATA_BLOCK + 7, out[1]);
    ASSERT_EQ(1, is_free(0));
    ASSERT_EQ(0, is_free(3));
    ASSERT_EQ(0, is_free(7));
}

/*
 * 0x00E3B3B8 `move.l D6,D3`: when the chunk runs out with the rescan flag
 * set, the search restarts at the CHUNK's start (D6), not at the
 * partition's start and not at the next chunk.
 *
 * The flag is set at 0x00E3B39E by a free block the stride made it skip.
 * Here the stride is 4, so the free block at relative 0x21 is passed over on
 * the first sweep of chunk [0x20,0x40); the rescan must come back to 0x20
 * and take it.  A restart at the partition's start (relative 0) would find
 * the decoy at relative 0x02 first.
 */
TEST(allocate_rescan_restarts_at_the_chunk_start)
{
    status_$t status = 0x7F;
    uint32_t out[4];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->step_blocks = 0;
    vol->bat_step = 4;                  /* the +0x12 longword is 4 */

    mark_free(0x02, 0x02);              /* decoy, before the chunk */
    mark_free(0x21, 0x21);              /* skipped by the stride, then taken */

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK + 0x20, 1, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + 0x21, out[0]);
    ASSERT_EQ(1, is_free(0x02));        /* the decoy was never reached */
    ASSERT_EQ(0, is_free(0x21));
}

/*
 * 0x00E3B3DA `tst.l (-0x208,A0)` / 0x00E3B3E0 `move.l (-0x14,A6),D3`: when
 * the search leaves a partition that STILL reports free blocks, it does not
 * move on - it restarts at that partition's own start and sweeps it again.
 *
 * Partition 0 is [0,0x200) and reports free blocks; the hint puts the search
 * at its far end with the only reachable free block back at relative 0x05.
 * Moving on to partition 1 instead would find the decoy at 0x205.
 */
TEST(allocate_rescans_a_partition_that_still_has_free_blocks)
{
    status_$t status = 0x7F;
    uint32_t out[4];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->num_partitions = 2;
    vol->alloc_chunk_size = 0x10;       /* small chunks, so the partition */
    vol->alloc_chunk_offset = 0;        /* test is reached quickly */
    vol->partitions[0].free_count = 5;  /* non-zero: sweep it again */
    vol->partitions[1].free_count = 0;

    mark_free(0x005, 0x005);            /* in partition 0 */
    mark_free(0x205, 0x205);            /* decoy, in partition 1 */

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK + 0x1F0, 1, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + 0x005, out[0]);
    ASSERT_EQ(1, is_free(0x205));       /* partition 1 was never entered */
    ASSERT_EQ(5 - 1, vol->partitions[0].free_count);
}

/*
 * 0x00E3B386 `subq.l #0x1,(-0x208,A0)`: each block taken decrements the free
 * count of the partition it belongs to, chosen by the CLAMPED block
 * (0x00E3B19A) rather than the raw hint.  A hint far past the end of the
 * volume clamps to the last block, which lives in the last partition.
 */
TEST(allocate_clamped_hint_selects_the_partition)
{
    status_$t status = 0x7F;
    uint32_t out[4];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    /* Only the final block of the volume is free. */
    mark_free(TOTAL_BLOCKS - 1, TOTAL_BLOCKS - 1);

    BAT_$ALLOCATE(TEST_VOL, 0x7FFFFFFF, 1, 0, out, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + TOTAL_BLOCKS - 1, out[0]);
    /* Relative 0x7FF + 0x100 = 0x8FF; partitions are 0x200 from 0x100. */
    ASSERT_EQ(0x100, vol->partitions[0].free_count);
    ASSERT_EQ(0x100 - 1, vol->partitions[3].free_count);
}

/* 0x00E3B2EA: a failed block load clears the cache and reports the error. */
TEST(allocate_load_failure_clears_the_cache)
{
    status_$t status = status_$ok;
    uint32_t out[4];

    reset_world();
    mark_free(0, 0x1F);
    mock_get_fail_after = 1;

    BAT_$ALLOCATE(TEST_VOL, FIRST_DATA_BLOCK, 1, 0, out, &status);

    ASSERT_STATUS(bat_$error, status);
    ASSERT_EQ(0, (unsigned long)(uintptr_t)bat_$cached_buffer);
    ASSERT_EQ(0, bat_$cached_vol);
}

/* ============================================================================
 * BAT_$FREE - ascending order
 * ============================================================================ */

/*
 * 0x00E3B588 stores the array base and 0x00E3B6C2 `addq.l #0x4,(-0x34,A6)`
 * walks it FORWARD, so blocks[0] is processed first.  With a load failure
 * armed for the second BAT block, only the entries up to the failure are
 * freed -- and which ones those are is exactly what the order decides.
 *
 * blocks[0] lives in mock block 0 and blocks[1] in mock block 1; the second
 * GET_BLOCK fails.  Ascending: blocks[0] is freed, blocks[1] is not.
 * Descending would free blocks[1]... which cannot even be reached, so the
 * observable difference is which bit ends up set.
 */
TEST(free_walks_the_array_ascending)
{
    status_$t status = status_$ok;
    uint32_t blocks[2];

    reset_world();
    /* Both entries must be in range, so the volume has to span two BAT blocks. */
    the_vol()->total_blocks = 0x4000;
    blocks[0] = FIRST_DATA_BLOCK + 0x0005;      /* mock BAT block 0 */
    blocks[1] = FIRST_DATA_BLOCK + 0x2005;      /* mock BAT block 1 */
    mock_get_fail_after = 2;                    /* the SECOND load fails */

    BAT_$FREE(blocks, 2, TEST_VOL, 0, &status);

    ASSERT_STATUS(bat_$error, status);
    ASSERT_EQ(1, is_free(0x0005));              /* the low entry got freed */
    ASSERT_EQ(0, is_free(0x2005));              /* the high entry did not */
}

/*
 * The same array with no failure: both bits set, and the free count rose by
 * two.  Order-independent, but it pins the loop count (0x00E3B6C6 subq/bcc
 * runs `count` times).
 */
TEST(free_frees_every_entry)
{
    status_$t status = 0x7F;
    uint32_t blocks[3];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->free_blocks = 0;
    blocks[0] = FIRST_DATA_BLOCK + 0x0001;
    blocks[1] = FIRST_DATA_BLOCK + 0x0002;
    blocks[2] = FIRST_DATA_BLOCK + 0x0003;

    BAT_$FREE(blocks, 3, TEST_VOL, 0, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(1, is_free(1));
    ASSERT_EQ(1, is_free(2));
    ASSERT_EQ(1, is_free(3));
    ASSERT_EQ(3, vol->free_blocks);
    ASSERT_EQ(0x100 + 3, vol->partitions[0].free_count);
}

/*
 * 0x00E3B592: a zero entry is not a block.  With `reserved` clear it moves
 * one block from the reserved pool to the free pool; ordering again matters,
 * because the counters are cumulative.
 */
TEST(free_zero_entry_moves_reserved_to_free)
{
    status_$t status = 0x7F;
    uint32_t blocks[3];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->free_blocks = 0;
    vol->reserved_blocks = 2;
    blocks[0] = 0;
    blocks[1] = 0;
    blocks[2] = 0;

    BAT_$FREE(blocks, 3, TEST_VOL, 0, &status);

    /* Two transfers succeed, the third finds the pool empty. */
    ASSERT_STATUS(bat_$error, status);
    ASSERT_EQ(2, vol->free_blocks);
    ASSERT_EQ(0, vol->reserved_blocks);
}

/* 0x00E3B594: with `reserved` set a zero entry is ignored outright. */
TEST(free_zero_entry_ignored_for_reserved_pool)
{
    status_$t status = 0x7F;
    uint32_t blocks[1];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->free_blocks = 7;
    vol->reserved_blocks = 0;
    blocks[0] = 0;

    BAT_$FREE(blocks, 1, TEST_VOL, 1, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(7, vol->free_blocks);
    ASSERT_EQ(0, vol->reserved_blocks);
}

/* 0x00E3B5B6 / 0x00E3B5BC: out-of-range blocks are reported, not freed. */
TEST(free_rejects_out_of_range_blocks)
{
    status_$t status = 0x7F;
    uint32_t blocks[1];

    reset_world();
    blocks[0] = FIRST_DATA_BLOCK - 1;           /* below first_data_block */
    BAT_$FREE(blocks, 1, TEST_VOL, 0, &status);
    ASSERT_STATUS(bat_$invalid_block, status);

    status = 0x7F;
    blocks[0] = FIRST_DATA_BLOCK + TOTAL_BLOCKS;
    BAT_$FREE(blocks, 1, TEST_VOL, 0, &status);
    ASSERT_STATUS(bat_$invalid_block, status);
}

/* 0x00E3B686: a block whose bit is already set was never allocated. */
TEST(free_rejects_an_already_free_block)
{
    status_$t status = 0x7F;
    uint32_t blocks[1];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->free_blocks = 0;
    mark_free(9, 9);
    blocks[0] = FIRST_DATA_BLOCK + 9;

    BAT_$FREE(blocks, 1, TEST_VOL, 0, &status);

    ASSERT_STATUS(bat_$error, status);
    ASSERT_EQ(0, vol->free_blocks);
}

/* 0x00E3B574..0x00E3B57A: a count of zero touches nothing at all. */
TEST(free_zero_count_does_nothing)
{
    status_$t status = 0x7F;
    uint32_t blocks[1];
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->free_blocks = 5;
    blocks[0] = FIRST_DATA_BLOCK + 1;

    BAT_$FREE(blocks, 0, TEST_VOL, 0, &status);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(5, vol->free_blocks);
    ASSERT_EQ(0, is_free(1));
    ASSERT_EQ(0, mock_get_calls);
}

/* ============================================================================
 * BAT_$ALLOC_VTOCE - the threshold compare
 * ============================================================================ */

/*
 * The partition holding absolute block b is (b - partition_start_offset) /
 * partition_size (0x00E3AF20 M$DIS$LLL), so with pso 0x100 and
 * partition_size 0x200 partition 2 covers [0x500, 0x700).
 */
#define HINT_IN_PARTITION_2 (FIRST_DATA_BLOCK + 2u * 0x200u + 4u)

/*
 * Give the hint partition (2) a chain head of its own and partition 0 a
 * different one, with a free count that wins the fallback search.  Then the
 * block BAT_$ALLOC_VTOCE reports says which arm ran: 0x777 means the hint
 * was kept, 0x111 means it was rejected and the search chose partition 0.
 */
static void setup_hint_vs_search(uint32_t hint_partition_free)
{
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();

    /* threshold = partition_size >> 3 = 0x200 >> 3 = 0x40 (0x00E3AF06) */
    vol->partitions[0].free_count = 0xFFFF;     /* wins the search */
    vol->partitions[1].free_count = 0x10;
    vol->partitions[2].free_count = hint_partition_free;
    vol->partitions[3].free_count = 0x10;

    BAT_SET_VTOCE_BLOCK(&vol->partitions[0], 0x111);
    BAT_SET_VTOCE_BLOCK(&vol->partitions[1], 0x222);
    BAT_SET_VTOCE_BLOCK(&vol->partitions[2], 0x777);
    BAT_SET_VTOCE_BLOCK(&vol->partitions[3], 0x333);
}

/*
 * 0x00E3AF46 `cmp.l (-0x208,A0),D3` computes threshold - free_count and
 * 0x00E3AF4A `bls` KEEPS the hint partition when threshold <= free_count.
 * A partition sitting EXACTLY on the threshold is therefore still usable,
 * and the routine must reuse ITS chain head rather than searching.
 */
TEST(alloc_vtoce_keeps_hint_at_exactly_the_threshold)
{
    status_$t status = 0x7F;
    uint32_t block = 0;
    int8_t new_flag = 0x7F;
    void *buf;

    setup_hint_vs_search(0x40);                 /* exactly the threshold */
    ((bat_$vtoce_block_t *)mock_vtoce_block)->entry_count = 0;

    buf = BAT_$ALLOC_VTOCE(TEST_VOL, HINT_IN_PARTITION_2,
                           &block, &status, &new_flag);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0x777, block);                    /* partition 2's own chain */
    ASSERT_EQ(0, new_flag);                     /* an existing block, not new */
    ASSERT_EQ((unsigned long)(uintptr_t)mock_vtoce_block,
              (unsigned long)(uintptr_t)buf);
    ASSERT_EQ(1, ((bat_$vtoce_block_t *)mock_vtoce_block)->entry_count);
}

/*
 * One below the threshold and the hint IS rejected (0x00E3AF4C moveq #-1),
 * so the search at 0x00E3AF56 runs and settles on the partition with the
 * largest free count - partition 0, whose chain head is a different block.
 */
TEST(alloc_vtoce_rejects_hint_below_the_threshold)
{
    status_$t status = 0x7F;
    uint32_t block = 0;
    int8_t new_flag = 0x7F;

    setup_hint_vs_search(0x3F);                 /* one below the threshold */

    (void)BAT_$ALLOC_VTOCE(TEST_VOL, HINT_IN_PARTITION_2,
                           &block, &status, &new_flag);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0x111, block);                    /* the search picked part 0 */
}

/*
 * 0x00E3AF3A `cmp.l D1,D0` / `bge`: a hint whose partition index is at or
 * past num_partitions is rejected outright, however much space it claims.
 */
TEST(alloc_vtoce_rejects_hint_past_the_last_partition)
{
    status_$t status = 0x7F;
    uint32_t block = 0;
    int8_t new_flag = 0x7F;
    bat_$volume_t *vol;

    /* Every in-range partition is poor, so the search must pick part 0. */
    setup_hint_vs_search(0x10);
    vol = the_vol();

    /*
     * Partition 4 exists in the record (BAT_MAX_PARTITIONS is 0x40) but is
     * past num_partitions, and it is stocked so that a missing range test
     * would take its chain head (0x444) instead of the search's 0x111.
     */
    vol->partitions[4].free_count = 0xFFFF;
    BAT_SET_VTOCE_BLOCK(&vol->partitions[4], 0x444);

    (void)BAT_$ALLOC_VTOCE(TEST_VOL, FIRST_DATA_BLOCK + 4u * 0x200u + 4u,
                           &block, &status, &new_flag);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0x111, block);                    /* fell through to the search */
}

/*
 * 0x00E3AFB2 `tst.w D5w` / 0x00E3AFB4 `bne`: the multiply arm runs for ANY
 * nonzero index, INCLUDING the -1 that survives when the search loop never
 * executes (0x00E3AF68 `bmi` skips it when num_partitions is 0).  The hint
 * handed to BAT_$ALLOCATE is then partition_size * -1 + partition_start_offset,
 * which is huge as an unsigned block number and clamps to the last block of
 * the volume (0x00E3B186).  A model that left the hint alone for -1 would
 * allocate from the FRONT of the volume instead.
 *
 * With num_partitions 0 the record's "partition -1" is the pair of cells at
 * +0x24/+0x28, so clearing unknown_28 gives that phantom entry a zero chain
 * head and sends the routine through BAT_$ALLOCATE, exactly as the image does.
 */
TEST(alloc_vtoce_computes_the_hint_for_index_minus_one)
{
    status_$t status = 0x7F;
    uint32_t block = 0;
    int8_t new_flag = 0x7F;
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->num_partitions = 0;            /* the search loop never runs */
    vol->unknown_28 = 0;                /* phantom entry -1 has no chain */

    /* A free block at each end of the volume tells the two hints apart. */
    mark_free(0, 0);
    mark_free(TOTAL_BLOCKS - 1, TOTAL_BLOCKS - 1);

    (void)BAT_$ALLOC_VTOCE(TEST_VOL, 0, &block, &status, &new_flag);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + TOTAL_BLOCKS - 1, block);
    ASSERT_EQ(1, is_free(0));           /* the front block was NOT taken */
    ASSERT_EQ(0, is_free(TOTAL_BLOCKS - 1));
}

/*
 * 0x00E3AF80..0x00E3AF8E: a partition whose status byte is 2 (free VTOCE
 * slots) and whose free count is strictly ABOVE the threshold ends the
 * search at once, ahead of any partition with a larger free count.  The
 * search starts at num_partitions >> 1 == 2 (0x00E3AF5C lsr.w #0x1).
 */
TEST(alloc_vtoce_type_2_partition_wins_the_search)
{
    status_$t status = 0x7F;
    uint32_t block = 0;
    int8_t new_flag = 0x7F;
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->partitions[0].free_count = 0xFFFF;     /* would win on count alone */
    vol->partitions[1].free_count = 0xFFFF;
    vol->partitions[2].free_count = 0x41;       /* just over the threshold */
    vol->partitions[2].status = 2;
    vol->partitions[3].free_count = 0xFFFF;
    BAT_SET_VTOCE_BLOCK(&vol->partitions[0], 0x111);
    BAT_SET_VTOCE_BLOCK(&vol->partitions[2], 0x555);

    /* No hint, so the search runs (0x00E3AF0C beq). */
    (void)BAT_$ALLOC_VTOCE(TEST_VOL, 0, &block, &status, &new_flag);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0x555, block);
    ASSERT_EQ(2, vol->partitions[2].status);    /* status byte preserved */
}

/*
 * 0x00E3AF86 `cmp.l (-0x208,A0),D3` / `bcc`: a type-2 partition sitting
 * exactly ON the threshold does NOT end the search - this arm is strict,
 * unlike the hint arm above.  Partition 0 then wins on free count.
 */
TEST(alloc_vtoce_type_2_at_the_threshold_does_not_win)
{
    status_$t status = 0x7F;
    uint32_t block = 0;
    int8_t new_flag = 0x7F;
    bat_$volume_t *vol;

    reset_world();
    vol = the_vol();
    vol->partitions[0].free_count = 0xFFFF;
    vol->partitions[1].free_count = 0x10;
    vol->partitions[2].free_count = 0x40;       /* exactly the threshold */
    vol->partitions[2].status = 2;
    vol->partitions[3].free_count = 0x10;
    BAT_SET_VTOCE_BLOCK(&vol->partitions[0], 0x111);
    BAT_SET_VTOCE_BLOCK(&vol->partitions[2], 0x555);

    (void)BAT_$ALLOC_VTOCE(TEST_VOL, 0, &block, &status, &new_flag);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0x111, block);
}

/*
 * 0x00E3AFEA `and.l (-0x204,A0),D0` with a zero chain head sends
 * BAT_$ALLOC_VTOCE through BAT_$ALLOCATE (0x00E3B012) for a fresh block,
 * which it then zeroes, stamps and installs as the partition's chain head
 * WITHOUT disturbing the status byte (0x00E3B08E / 0x00E3B098).
 */
TEST(alloc_vtoce_initialises_a_new_block)
{
    status_$t status = 0x7F;
    uint32_t block = 0;
    int8_t new_flag = 0;
    bat_$volume_t *vol;
    bat_$vtoce_block_t *vtoce;
    int i;

    reset_world();
    vol = the_vol();
    vol->partitions[2].free_count = 0xFFFF;
    vol->partitions[2].status = 2;
    BAT_SET_VTOCE_BLOCK(&vol->partitions[2], 0);    /* no chain yet */

    /* BAT_$ALLOCATE must be able to find a block inside partition 2. */
    mark_free(0x400, 0x41F);
    memset(mock_vtoce_block, 0xAA, sizeof(mock_vtoce_block));

    (void)BAT_$ALLOC_VTOCE(TEST_VOL, FIRST_DATA_BLOCK + 0x400,
                           &block, &status, &new_flag);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(FIRST_DATA_BLOCK + 0x400, block);
    ASSERT_EQ((int8_t)-1, new_flag);                /* 0x00E3B08A st (A4) */

    /* The chain head took the new block; the status byte survived. */
    ASSERT_EQ(block & 0xFFFFFF, BAT_GET_VTOCE_BLOCK(&vol->partitions[2]));
    ASSERT_EQ(2, vol->partitions[2].status);

    vtoce = (bat_$vtoce_block_t *)mock_vtoce_block;
    ASSERT_EQ(VTOCE_MAGIC, vtoce->magic);           /* 0x00E3B07A */
    ASSERT_EQ(block, vtoce->self_block);            /* 0x00E3B082 */
    ASSERT_EQ(1, vtoce->entry_count);               /* 0x00E3B09C */

    /*
     * 0x00E3B06A `move.w #0xfd,D0w` + dbf clears 0xFE longwords, i.e.
     * mock_vtoce_block[0 .. 0x3F7]; only the magic and self-block follow.
     */
    for (i = 8; i < 0x3F8; i++) {
        if (mock_vtoce_block[i] != 0) {
            printf("FAILED\n    byte 0x%X not cleared at line %d\n", i, __LINE__);
            tests_failed++;
            return;
        }
    }
}

/*
 * 0x00E3B0A0..0x00E3B0B6: the third entry fills the block, so the chain head
 * advances to the block's successor - again an unmasked or that preserves
 * the status byte.
 */
TEST(alloc_vtoce_full_block_advances_the_chain)
{
    status_$t status = 0x7F;
    uint32_t block = 0;
    int8_t new_flag = 0x7F;
    bat_$volume_t *vol;
    bat_$vtoce_block_t *vtoce;

    reset_world();
    vol = the_vol();
    vol->partitions[2].free_count = 0xFFFF;
    vol->partitions[2].status = 1;
    BAT_SET_VTOCE_BLOCK(&vol->partitions[2], 0x777);

    vtoce = (bat_$vtoce_block_t *)mock_vtoce_block;
    vtoce->entry_count = VTOCE_ENTRIES_PER_BLOCK - 1;
    vtoce->next_vtoce = 0x888;

    (void)BAT_$ALLOC_VTOCE(TEST_VOL, HINT_IN_PARTITION_2,
                           &block, &status, &new_flag);

    ASSERT_STATUS(status_$ok, status);
    ASSERT_EQ(0x777, block);
    ASSERT_EQ(VTOCE_ENTRIES_PER_BLOCK, vtoce->entry_count);
    ASSERT_EQ(0x888, BAT_GET_VTOCE_BLOCK(&vol->partitions[2]));
    ASSERT_EQ(1, vol->partitions[2].status);
}

/*
 * 0x00E3AEDA `clr.l (-0x14,A6)` / 0x00E3B0C8: every failure path returns the
 * cleared result cell, i.e. NULL.
 */
TEST(alloc_vtoce_returns_null_on_a_failed_fetch)
{
    status_$t status = status_$ok;
    uint32_t block = 0;
    int8_t new_flag = 0x7F;
    bat_$volume_t *vol;
    void *buf;

    reset_world();
    vol = the_vol();
    vol->partitions[2].free_count = 0xFFFF;
    BAT_SET_VTOCE_BLOCK(&vol->partitions[2], 0x777);
    mock_get_fail_after = 1;

    buf = BAT_$ALLOC_VTOCE(TEST_VOL, HINT_IN_PARTITION_2,
                           &block, &status, &new_flag);

    ASSERT_STATUS(bat_$error, status);
    ASSERT_EQ(0, (unsigned long)(uintptr_t)buf);
    ASSERT_EQ(0, new_flag);                     /* 0x00E3AEE2 clr.b (A0) */
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    printf("=== BAT bitmap search tests ===\n\n");

    printf("BAT_$ALLOCATE argument words:\n");
    RUN_TEST(allocate_count_comes_from_the_first_word);
    RUN_TEST(allocate_reserved_flag_comes_from_the_second_word);
    RUN_TEST(allocate_reserved_pool_capacity_is_checked_alone);
    RUN_TEST(allocate_single_block_from_free_pool);
    RUN_TEST(allocate_old_format_keeps_the_0xb_cushion);
    RUN_TEST(allocate_rejects_unmounted_volume);

    printf("BAT_$ALLOCATE chunk advance:\n");
    RUN_TEST(allocate_next_chunk_starts_at_old_chunk_end);
    RUN_TEST(allocate_walks_forward_chunk_by_chunk);
    RUN_TEST(allocate_crosses_a_bat_block_boundary);
    RUN_TEST(allocate_stride_comes_from_the_step_longword);
    RUN_TEST(allocate_rescan_restarts_at_the_chunk_start);
    RUN_TEST(allocate_rescans_a_partition_that_still_has_free_blocks);
    RUN_TEST(allocate_clamped_hint_selects_the_partition);
    RUN_TEST(allocate_load_failure_clears_the_cache);

    printf("BAT_$FREE:\n");
    RUN_TEST(free_walks_the_array_ascending);
    RUN_TEST(free_frees_every_entry);
    RUN_TEST(free_zero_entry_moves_reserved_to_free);
    RUN_TEST(free_zero_entry_ignored_for_reserved_pool);
    RUN_TEST(free_rejects_out_of_range_blocks);
    RUN_TEST(free_rejects_an_already_free_block);
    RUN_TEST(free_zero_count_does_nothing);

    printf("BAT_$ALLOC_VTOCE:\n");
    RUN_TEST(alloc_vtoce_keeps_hint_at_exactly_the_threshold);
    RUN_TEST(alloc_vtoce_rejects_hint_below_the_threshold);
    RUN_TEST(alloc_vtoce_rejects_hint_past_the_last_partition);
    RUN_TEST(alloc_vtoce_computes_the_hint_for_index_minus_one);
    RUN_TEST(alloc_vtoce_type_2_partition_wins_the_search);
    RUN_TEST(alloc_vtoce_type_2_at_the_threshold_does_not_win);
    RUN_TEST(alloc_vtoce_initialises_a_new_block);
    RUN_TEST(alloc_vtoce_full_block_advances_the_chain);
    RUN_TEST(alloc_vtoce_returns_null_on_a_failed_fetch);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
