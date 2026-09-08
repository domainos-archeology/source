/*
 * bat/test/test_partition_table.c - bat_$volume_t layout and the two
 * label <-> volume copies performed by BAT_$MOUNT (0x00E3B6F8) and
 * BAT_$DISMOUNT (0x00E3B8BE).
 *
 * The real bat/mount.c and bat/dismount.c are #included below and driven
 * through a mock DBUF that hands back a 1024-byte disk block, so the tests
 * observe exactly the bytes those routines move.  The point of the tests is
 * the EXTENT of each copy:
 *
 *   0x00E3B7E2  moveq  #0x7,D6      -> 8 longwords, label +0x2C <-> vol +0x00
 *   0x00E3B7F2  move.w #0x82,D6w    -> 0x83 longwords, label +0xFC <-> vol +0x20
 *
 * The second one ends at vol +0x22B, one longword below alloc_chunk_size at
 * vol +0x22C, which BAT_$MOUNT computes from the drive geometry afterwards
 * (0x00E3B838 / 0x00E3B874).  A loop that moved twice as much would run off
 * the end of the 0x234-byte record (bead source-ffrk).
 */

#include "bat/bat_internal.h"

/*
 * DISK_VOLUME_BASE is a fixed m68k address (0x00E7A1CC), so the host build
 * points DISK_VOL() at a mock buffer.  The stride stays DISK_VOLUME_SIZE
 * (0x48) exactly as the machine code computes it (0x00E3B826-0x00E3B830).
 */
static uint8_t mock_disk_data[0x1000];
#undef DISK_VOLUME_BASE
#define DISK_VOLUME_BASE (mock_disk_data)

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

/* The volume label lives in a real 1024-byte disk block. */
#define DISK_BLOCK_SIZE 1024
static uint8_t mock_block[DISK_BLOCK_SIZE];
static status_$t mock_get_status;
static uint16_t mock_last_set_flags;

uint32_t TIME_$CURRENT_CLOCKH;
uint32_t TIME_$BOOT_TIME;
uint32_t NODE_$ME;
uid_t LV_LABEL_$UID;

void ML_$LOCK(int16_t id) { (void)id; }
void ML_$UNLOCK(int16_t id) { (void)id; }

void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    (void)vol_idx; (void)block; (void)uid; (void)block_hint;
    (void)block_type; (void)flags;
    *status = mock_get_status;
    return mock_block;
}

void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    (void)buffer;
    mock_last_set_flags = flags;
    *status = status_$ok;
}

ulong M$MIU$LLW(ulong multiplicand, ushort multiplier)
{
    return multiplicand * (ulong)multiplier;
}

long M$OIS$LLL(long dividend, long divisor)
{
    return divisor == 0 ? 0 : dividend % divisor;
}

/* Implementation and data under test (included directly) */
#include "../bat_data.c"
#include "../mount.c"
#include "../dismount.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

#define TEST_VOL 1      /* volume indices run 1..6 (0x00E3BA1A / 0x00E3BA1E) */

static bat_$label_t *label_of(void) { return (bat_$label_t *)mock_block; }

/* Fill a byte range with a position-dependent pattern. */
static void fill_pattern(uint8_t *p, size_t n, uint8_t seed)
{
    size_t i;
    for (i = 0; i < n; i++) {
        p[i] = (uint8_t)(seed + i * 7u);
    }
}

static void reset_world(void)
{
    memset(mock_block, 0, sizeof(mock_block));
    memset(bat_$volumes, 0, sizeof(bat_$volumes));
    memset(bat_$mounted, 0, sizeof(bat_$mounted));
    memset(bat_$volume_flags, 0, sizeof(bat_$volume_flags));
    memset(mock_disk_data, 0, sizeof(mock_disk_data));
    bat_$cached_buffer = NULL;
    bat_$cached_vol = 0;
    mock_get_status = status_$ok;
    TIME_$CURRENT_CLOCKH = 0x11223344;
    TIME_$BOOT_TIME = 0x55667788;
    NODE_$ME = 0x0000ABCD;

    /* One track's worth of blocks, so BAT_$MOUNT's geometry math is defined. */
    DISK_VOL(TEST_VOL)->blocks_per_cyl = 32;
    DISK_VOL(TEST_VOL)->part_volx[0] = 0;
}

/* ============================================================================
 * Layout
 * ============================================================================ */

TEST(volume_record_layout)
{
    ASSERT_EQ(0x234, sizeof(bat_$volume_t));
    ASSERT_EQ(0x000, __builtin_offsetof(bat_$volume_t, total_blocks));
    ASSERT_EQ(0x004, __builtin_offsetof(bat_$volume_t, free_blocks));
    ASSERT_EQ(0x008, __builtin_offsetof(bat_$volume_t, bat_block_start));
    ASSERT_EQ(0x00C, __builtin_offsetof(bat_$volume_t, first_data_block));
    ASSERT_EQ(0x012, __builtin_offsetof(bat_$volume_t, step_blocks));
    ASSERT_EQ(0x014, __builtin_offsetof(bat_$volume_t, bat_step));
    ASSERT_EQ(0x018, __builtin_offsetof(bat_$volume_t, reserved_blocks));
    ASSERT_EQ(0x020, __builtin_offsetof(bat_$volume_t, num_partitions));
    ASSERT_EQ(0x022, __builtin_offsetof(bat_$volume_t, partition_start_offset));
    ASSERT_EQ(0x024, __builtin_offsetof(bat_$volume_t, partition_size));
    ASSERT_EQ(0x028, __builtin_offsetof(bat_$volume_t, unknown_28));
    ASSERT_EQ(0x02C, __builtin_offsetof(bat_$volume_t, partitions));
    ASSERT_EQ(0x22C, __builtin_offsetof(bat_$volume_t, alloc_chunk_size));
    ASSERT_EQ(0x230, __builtin_offsetof(bat_$volume_t, alloc_chunk_offset));
}

/*
 * The partition index is scaled by `lsl.l #0x3` at 0x00E3AD74, 0x00E3AE8E,
 * 0x00E3AF40, 0x00E3AF74, 0x00E3AFE2, 0x00E3B380, 0x00E3B3D4 and 0x00E3B6B8,
 * so the stride is 8 and 0x40 entries exactly fill 0x2C..0x22B.
 */
TEST(partition_stride)
{
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];

    ASSERT_EQ(8, sizeof(bat_$partition_t));
    ASSERT_EQ(0x40, BAT_MAX_PARTITIONS);

    /* free_count is (-0x208,An)+i*8, status is (-0x204,An)+i*8 */
    ASSERT_EQ(0x2C, (uint8_t *)&vol->partitions[0].free_count - (uint8_t *)vol);
    ASSERT_EQ(0x30, (uint8_t *)&vol->partitions[0].status - (uint8_t *)vol);
    ASSERT_EQ(8, (uint8_t *)&vol->partitions[1] - (uint8_t *)&vol->partitions[0]);

    /* The array ends exactly where alloc_chunk_size begins. */
    ASSERT_EQ((uint8_t *)&vol->alloc_chunk_size - (uint8_t *)vol,
              (uint8_t *)&vol->partitions[BAT_MAX_PARTITIONS] - (uint8_t *)vol);
}

/* The label mirrors the copied region byte for byte. */
TEST(label_partition_region)
{
    ASSERT_EQ(0x0FC, __builtin_offsetof(bat_$label_t, num_partitions));
    ASSERT_EQ(0x0FE, __builtin_offsetof(bat_$label_t, partition_start_offset));
    ASSERT_EQ(0x100, __builtin_offsetof(bat_$label_t, partition_size));
    ASSERT_EQ(0x104, __builtin_offsetof(bat_$label_t, unknown_104));
    ASSERT_EQ(0x108, __builtin_offsetof(bat_$label_t, partition_table));
    ASSERT_EQ(0x308, __builtin_offsetof(bat_$label_t, num_partitions) +
                     BAT_PART_TABLE_LONGWORDS * 4);
}

/* ============================================================================
 * Module data segment (0xE79478..0xE7A1CB, map `D E79478 BAT_ size = D54`)
 * ============================================================================ */

/*
 * The scalars start where the six volume records end and the last one ends
 * where the segment does.  Every address below is the (disp,A5) displacement
 * of an accessor plus A5 = 0xE79478.
 */
TEST(module_scalar_addresses)
{
    ASSERT_EQ(0x00e79478u, BAT_DATA_BASE);
    ASSERT_EQ(0x00000d54u, BAT_DATA_SIZE);
    ASSERT_EQ(0x00e7a1ccu, BAT_DATA_END);

    /* 6 * 0x234 = 0xD38 bytes of volume records (0x00E3B756 mulu.w #0x234) */
    ASSERT_EQ(BAT_CACHED_BUFFER_ADDR,
              BAT_DATA_BASE + BAT_MAX_VOL_INDEX * 0x234u);
    ASSERT_EQ(BAT_DATA_BASE, BAT_VOLUMES_BASE + 0x234u);

    ASSERT_EQ(0x00e7a1b0u, BAT_CACHED_BUFFER_ADDR);   /* (0xd38,A5) */
    ASSERT_EQ(0x00e7a1b4u, BAT_CACHED_BLOCK_ADDR);    /* (0xd3c,A5) */
    ASSERT_EQ(0x00e7a1b7u, BAT_VOLUME_FLAGS_BASE);    /* (0xd3f,An) */
    ASSERT_EQ(0x00e7a1bfu, BAT_MOUNTED_BASE);         /* (0xd47,An) */
    ASSERT_EQ(0x00e7a1c6u, BAT_CACHED_DIRTY_ADDR);    /* (0xd4e,A5) */
    ASSERT_EQ(0x00e7a1c8u, BAT_CACHED_VOL_ADDR);      /* (0xd50,A5) */

    /* Live (non-phantom) extents of the two biased byte arrays. */
    ASSERT_EQ(0x00e7a1b8u, BAT_VOLUME_FLAGS_BASE + 1u);
    ASSERT_EQ(0x00e7a1bdu, BAT_VOLUME_FLAGS_BASE + BAT_MAX_VOL_INDEX);
    ASSERT_EQ(0x00e7a1c0u, BAT_MOUNTED_BASE + 1u);
    ASSERT_EQ(0x00e7a1c5u, BAT_MOUNTED_BASE + BAT_MAX_VOL_INDEX);

    /* The last scalar plus its two tail bytes reaches the segment end. */
    ASSERT_EQ(BAT_DATA_END, BAT_CACHED_VOL_ADDR + 2u + 2u);
}

/*
 * The two per-volume arrays are byte arrays with stride 1: every accessor
 * forms its address with `lea (0x0,A5,Dnw*0x1),An` and reaches it with a
 * byte-wide instruction (0x00E3B764 move.b, 0x00E3B792 st, 0x00E3B72A clr.b).
 * A wider element type would make bat_$volume_flags overlap
 * bat_$cached_block, which is bead source-uu78.
 */
TEST(module_scalar_types)
{
    ASSERT_EQ(1, sizeof(bat_$volume_flags[0]));
    ASSERT_EQ(1, sizeof(bat_$mounted[0]));
    ASSERT_EQ(BAT_MAX_VOL_INDEX + 1, sizeof(bat_$volume_flags));
    ASSERT_EQ(BAT_MAX_VOL_INDEX + 1, sizeof(bat_$mounted));
    ASSERT_EQ(2, sizeof(bat_$cached_dirty));
    ASSERT_EQ(2, sizeof(bat_$cached_vol));
    ASSERT_EQ(4, sizeof(bat_$cached_block));

    /* Both arrays are signed: every reader branches on the sign bit. */
    bat_$volume_flags[TEST_VOL] = (int8_t)0xFF;
    ASSERT_EQ(1, bat_$volume_flags[TEST_VOL] < 0);
    bat_$mounted[TEST_VOL] = (int8_t)0xFF;
    ASSERT_EQ(1, bat_$mounted[TEST_VOL] < 0);
    bat_$volume_flags[TEST_VOL] = 0;
    bat_$mounted[TEST_VOL] = 0;
}

/*
 * The record BAT_$MOUNT reads is disk_$volume_t (disk/disk.h), one DISK_$DVTBL
 * entry reached at negative displacements from 0xE7A290 + vol*0x48
 * (0x00E3B820-0x00E3B830), i.e. with the same 0x48 bias DISK_VOL() uses.
 *
 * disk/disk.h asserts the full m68k offset set (lv_start +0x08,
 * blocks_per_cyl +0x24, num_parts +0x2c, part_volx +0x36) under ARCH_M68K;
 * the host build cannot repeat it because dev_info (+0x18) is a native
 * pointer, so what is checked here is the addressing itself.
 */
TEST(disk_info_layout)
{
    ASSERT_EQ(0x48, DISK_VOLUME_SIZE);
    ASSERT_EQ(0x7c, DISK_VOL_DESC_OFFSET);
    ASSERT_EQ(0x08, __builtin_offsetof(disk_$volume_t, lv_start));

    /* vol*0x48 stride (0x00E3B826 lsl.w #3 / 0x00E3B82A lsl.w #3 / add.w) */
    ASSERT_EQ(0x48, (uint8_t *)DISK_VOL(2) - (uint8_t *)DISK_VOL(1));

    /* Biased base: entry 1 is DISK_$DVTBL itself. */
    ASSERT_EQ(0x00e7a290u, 0x00e7a1ccu + 1u * 0x48u + 0x7cu);
    ASSERT_EQ(0x00e7a290u, 0x00e7a248u + 1u * 0x48u);
}

/* ============================================================================
 * BAT_$MOUNT: label -> volume record
 * ============================================================================ */

TEST(mount_copies_exactly_0x83_longwords)
{
    status_$t status;
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];
    uint8_t *volbytes = (uint8_t *)vol;
    uint8_t guard[0x234 - 0x22C];
    int i;

    reset_world();

    /* New-format label, clean, with a distinctive partition region. */
    label_of()->version = 1;
    fill_pattern(mock_block + 0xFC, BAT_PART_TABLE_LONGWORDS * 4, 0x41);

    /* Poison the whole record so an over-copy is visible as a wipe. */
    memset(volbytes, 0xA5, sizeof(*vol));
    memcpy(guard, volbytes + 0x22C, sizeof(guard));

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);

    /* 0x20C bytes landed at +0x20 ... */
    ASSERT_EQ(0, memcmp(volbytes + 0x20, mock_block + 0xFC,
                        BAT_PART_TABLE_LONGWORDS * 4));

    /* ... and nothing past +0x22B came from the label. */
    for (i = 0; i < (int)sizeof(guard); i++) {
        if (volbytes[0x22C + i] == (uint8_t)(0x41 + (0x20C + i) * 7u)) {
            printf("FAILED\n    copy overran into +0x%X at line %d\n",
                   0x22C + i, __LINE__);
            tests_failed++;
            return;
        }
    }

    /* +0x22C / +0x230 hold the geometry BAT_$MOUNT computed, not label bytes. */
    ASSERT_EQ(32, vol->alloc_chunk_size);
    ASSERT_EQ(0x234, sizeof(bat_$volume_t));
    (void)guard;
}

TEST(mount_copies_bat_header)
{
    status_$t status;
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];

    reset_world();

    label_of()->version = 1;
    label_of()->total_blocks = 0x00010000;
    label_of()->free_blocks = 0x00008000;
    label_of()->bat_block_start = 0x00000010;
    label_of()->first_data_block = 0x00000020;
    label_of()->step_blocks = 0x0001;
    label_of()->bat_step = 0x0003;
    label_of()->reserved_blocks = 0x00000040;
    label_of()->unknown_48 = 0xDEADBEEF;

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);

    /* All 8 longwords move, including the two the field-by-field copy missed. */
    ASSERT_EQ(0x00010000, vol->total_blocks);
    ASSERT_EQ(0x00008000, vol->free_blocks);
    ASSERT_EQ(0x00000010, vol->bat_block_start);
    ASSERT_EQ(0x00000020, vol->first_data_block);
    ASSERT_EQ(0x0001, vol->step_blocks);
    ASSERT_EQ(0x0003, vol->bat_step);
    ASSERT_EQ(0x00000040, vol->reserved_blocks);
    ASSERT_EQ(0xDEADBEEF, vol->unknown_1c);
    /* volume_trouble is copied AFTER 0x00E3B79C rewrote bit 12 of the label. */
    ASSERT_EQ(label_of()->volume_trouble, vol->volume_trouble);
}

/*
 * An old-format label (version 0) gets a single partition covering the whole
 * volume: 0x00E3B802 partition_size = 0x7FFFFFFF, 0x00E3B80A num_partitions = 1,
 * 0x00E3B810-0x00E3B81A partitions[0].free_count = free_blocks - 0xB.
 */
TEST(mount_old_format_single_partition)
{
    status_$t status;
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];

    reset_world();

    label_of()->version = 0;
    label_of()->free_blocks = 1000;

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);

    ASSERT_EQ(0x7FFFFFFF, vol->partition_size);
    ASSERT_EQ(1, vol->num_partitions);
    ASSERT_EQ(1000 - 0xB, vol->partitions[0].free_count);
}

/* ============================================================================
 * BAT_$DISMOUNT: volume record -> label
 * ============================================================================ */

TEST(dismount_copies_exactly_0x83_longwords)
{
    status_$t status;
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];
    uint8_t *volbytes = (uint8_t *)vol;
    int i;

    reset_world();

    bat_$mounted[TEST_VOL] = (int8_t)0xFF;
    bat_$volume_flags[TEST_VOL] = (int8_t)-1;   /* new format: copy back */

    /* Distinctive bytes across the WHOLE record, including +0x22C..+0x233. */
    fill_pattern(volbytes, sizeof(*vol), 0x11);
    /* Label starts as 0xEE everywhere so an over-copy is visible past 0x307. */
    memset(mock_block, 0xEE, sizeof(mock_block));
    ((bat_$label_t *)mock_block)->version = 1;

    BAT_$DISMOUNT(TEST_VOL, 0, &status);
    ASSERT_EQ(status_$ok, status);

    /* 0x20C bytes landed at label +0xFC. */
    ASSERT_EQ(0, memcmp(mock_block + 0xFC, volbytes + 0x20,
                        BAT_PART_TABLE_LONGWORDS * 4));

    /* Nothing past label +0x307 was touched. */
    for (i = 0x308; i < DISK_BLOCK_SIZE; i++) {
        if (mock_block[i] != 0xEE) {
            printf("FAILED\n    copy overran into label +0x%X at line %d\n",
                   i, __LINE__);
            tests_failed++;
            return;
        }
    }

    /* The BAT header round-tripped, all 8 longwords of it. */
    ASSERT_EQ(0, memcmp(mock_block + 0x2C, volbytes + 0x00,
                        BAT_HEADER_LONGWORDS * 4));
}

/*
 * 0x00E3B96C-0x00E3B970: an old-format volume (flag byte >= 0) skips the
 * partition copy-back entirely, but still writes the BAT header.
 */
TEST(dismount_old_format_skips_partition_copy)
{
    status_$t status;
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];
    uint8_t *volbytes = (uint8_t *)vol;

    reset_world();

    bat_$mounted[TEST_VOL] = (int8_t)0xFF;
    bat_$volume_flags[TEST_VOL] = 0;            /* old format */

    fill_pattern(volbytes, sizeof(*vol), 0x11);
    memset(mock_block + 0xFC, 0xEE, BAT_PART_TABLE_LONGWORDS * 4);

    BAT_$DISMOUNT(TEST_VOL, 0, &status);
    ASSERT_EQ(status_$ok, status);

    ASSERT_EQ(0xEE, mock_block[0xFC]);
    ASSERT_EQ(0xEE, mock_block[0x307]);
    ASSERT_EQ(0, memcmp(mock_block + 0x2C, volbytes + 0x00,
                        BAT_HEADER_LONGWORDS * 4));
}

/* A negative `flags` clears total_blocks and writes no label (0x00E3B9C0). */
TEST(dismount_negative_flags_clears_total_blocks)
{
    status_$t status;
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];

    reset_world();

    bat_$mounted[TEST_VOL] = (int8_t)0xFF;
    vol->total_blocks = 0x1234;
    memset(mock_block, 0xEE, sizeof(mock_block));

    BAT_$DISMOUNT(TEST_VOL, -1, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, vol->total_blocks);
    ASSERT_EQ(0xEE, mock_block[0x2C]);
    ASSERT_EQ(0, bat_$mounted[TEST_VOL]);
}

/*
 * 0x00E3B986..0x00E3B990: BAT_$DISMOUNT reads the clock ONCE and stores it
 * into the label's +0xB0 and +0xC0.  It does NOT touch +0xBC
 * (dismount_time), which only BAT_$MOUNT writes (0x00E3B7D0).
 *
 *   0x00E3B986  move.l (0x00e2b0e4).l,D0
 *   0x00E3B98C  move.l D0,(0xb0,A0)
 *   0x00E3B990  move.l D0,(0xc0,A0)
 *   0x00E3B994  clr.w (0xce,A0)
 */
TEST(dismount_stamps_current_time_not_dismount_time)
{
    status_$t status;
    bat_$label_t *label = label_of();

    reset_world();
    bat_$mounted[TEST_VOL] = (int8_t)0xFF;

    TIME_$CURRENT_CLOCKH = 0xC0FFEE01;
    label->mount_time_high = 0xDEAD0000;
    label->dismount_time   = 0xDEAD00BC;
    label->current_time    = 0xDEAD00C0;
    label->salvage_flag    = 1;

    BAT_$DISMOUNT(TEST_VOL, 0, &status);
    ASSERT_EQ(status_$ok, status);

    ASSERT_EQ(0xC0FFEE01, label->mount_time_high);   /* +0xB0 written */
    ASSERT_EQ(0xC0FFEE01, label->current_time);      /* +0xC0 written */
    ASSERT_EQ(0xDEAD00BC, label->dismount_time);     /* +0xBC untouched */
    ASSERT_EQ(0, label->salvage_flag);               /* 0x00E3B994 */
}

/*
 * 0x00E3B7B6..0x00E3B7C4: BAT_$MOUNT keeps the label's top 12 bits and ORs
 * in the WHOLE NODE_$ME longword -- `or.l D6,(0xb4,A0)` applies no mask, so
 * a node id with bits set above bit 19 does reach the preserved field.
 */
TEST(mount_ors_node_me_without_masking)
{
    status_$t status;
    bat_$label_t *label = label_of();

    reset_world();
    label->version = 1;

    /*
     * Bit 23 is set in NODE_$ME and CLEAR in the preserved top 12 bits, so
     * masking NODE_$ME to 20 bits would lose it.
     */
    NODE_$ME = 0x00812345;
    label->mount_time_low = 0x30000000;

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);

    ASSERT_EQ(0x30812345u, label->mount_time_low);
}

/*
 * 0x00E3B7AA `tst.l (0x3e,A0)` treats the (step_blocks, bat_step) pair at
 * label +0x3E as ONE longword and 0x00E3B7B2 `move.l D6,(0x3e,A0)` with
 * D6 = 3 defaults it, leaving step_blocks 0 and bat_step 3.
 */
TEST(mount_defaults_the_step_longword_to_three)
{
    status_$t status;
    bat_$label_t *label = label_of();
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];

    reset_world();
    label->version = 1;
    label->step_blocks = 0;
    label->bat_step = 0;

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, label->step_blocks);
    ASSERT_EQ(3, label->bat_step);
    /* The header copy carried the defaulted pair into the volume record. */
    ASSERT_EQ(0, vol->step_blocks);
    ASSERT_EQ(3, vol->bat_step);

    /* A non-zero pair is left alone, even when step_blocks alone is zero. */
    reset_world();
    label = label_of();
    label->version = 1;
    label->step_blocks = 0;
    label->bat_step = 7;

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(7, label->bat_step);
}

/* ============================================================================
 * Round trip
 * ============================================================================ */

TEST(mount_dismount_round_trip)
{
    status_$t status;
    uint8_t original[BAT_PART_TABLE_LONGWORDS * 4];

    reset_world();

    label_of()->version = 1;
    fill_pattern(mock_block + 0xFC, sizeof(original), 0x5A);
    memcpy(original, mock_block + 0xFC, sizeof(original));

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);

    /* Scribble over the label so the copy-back is what restores it. */
    memset(mock_block + 0xFC, 0, sizeof(original));

    BAT_$DISMOUNT(TEST_VOL, 0, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, memcmp(mock_block + 0xFC, original, sizeof(original)));
}

/*
 * 0x00E3B762 `sne D0b` / 0x00E3B764 `move.b D0b,(0xd3f,A2)` write ONE byte.
 * Modelling the cell as a longword would have this store land on
 * bat_$cached_block, which shares the longword at 0xE7A1B4 with the array's
 * phantom element 0 (bead source-uu78).
 */
TEST(mount_writes_one_flag_byte)
{
    status_$t status;

    reset_world();
    bat_$cached_block = 0xDEADBEEF;
    bat_$volume_flags[TEST_VOL - 1] = 0x11;
    bat_$volume_flags[TEST_VOL + 1] = 0x22;

    label_of()->version = 1;
    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)bat_$volume_flags[TEST_VOL]);
    ASSERT_EQ((int8_t)0xFF, bat_$mounted[TEST_VOL]);

    /* Neighbours and the cached-block cell are untouched. */
    ASSERT_EQ(0x11, bat_$volume_flags[TEST_VOL - 1]);
    ASSERT_EQ(0x22, bat_$volume_flags[TEST_VOL + 1]);
    ASSERT_EQ(0xDEADBEEF, bat_$cached_block);

    /* version == 0 stores a plain zero (`sne` of a zero word). */
    reset_world();
    bat_$cached_block = 0xDEADBEEF;
    label_of()->version = 0;
    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, bat_$volume_flags[TEST_VOL]);
    ASSERT_EQ(0xDEADBEEF, bat_$cached_block);
}

/*
 * The allocation-chunk geometry comes from three DIFFERENT DISK_$DVTBL
 * cells: blocks_per_cyl (+0x24), num_parts (+0x2C, only when
 * interleave_mode (+0x36) is 1) and lv_start (+0x08).
 *
 *   alloc_chunk_size   = blocks_per_cyl [* num_parts]
 *   alloc_chunk_offset = size - ((first_data_block + lv_start) mod size)
 */
TEST(mount_chunk_geometry_from_dvtbl)
{
    status_$t status;
    bat_$volume_t *vol = &bat_$volumes[TEST_VOL];

    reset_world();
    label_of()->version = 1;
    label_of()->first_data_block = 5;
    DISK_VOL(TEST_VOL)->blocks_per_cyl = 32;
    DISK_VOL(TEST_VOL)->num_parts = 4;
    DISK_VOL(TEST_VOL)->part_volx[0] = 0;
    DISK_VOL(TEST_VOL)->lv_start = 3;

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(32, vol->alloc_chunk_size);
    ASSERT_EQ(32 - ((5 + 3) % 32), vol->alloc_chunk_offset);

    /* interleave_mode 1 scales the chunk by num_parts (0x00E3B83C/0x00E3B84C) */
    reset_world();
    label_of()->version = 1;
    label_of()->first_data_block = 5;
    DISK_VOL(TEST_VOL)->blocks_per_cyl = 32;
    DISK_VOL(TEST_VOL)->num_parts = 4;
    DISK_VOL(TEST_VOL)->part_volx[0] = 1;
    DISK_VOL(TEST_VOL)->lv_start = 3;

    BAT_$MOUNT(TEST_VOL, (int8_t)0x80, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(128, vol->alloc_chunk_size);
    ASSERT_EQ(128 - ((5 + 3) % 128), vol->alloc_chunk_offset);
}

int main(void)
{
    printf("=== BAT partition-table layout and copy tests ===\n");

    RUN_TEST(module_scalar_addresses);
    RUN_TEST(module_scalar_types);
    RUN_TEST(disk_info_layout);
    RUN_TEST(volume_record_layout);
    RUN_TEST(partition_stride);
    RUN_TEST(label_partition_region);
    RUN_TEST(mount_copies_exactly_0x83_longwords);
    RUN_TEST(mount_copies_bat_header);
    RUN_TEST(mount_old_format_single_partition);
    RUN_TEST(dismount_copies_exactly_0x83_longwords);
    RUN_TEST(dismount_old_format_skips_partition_copy);
    RUN_TEST(dismount_negative_flags_clears_total_blocks);
    RUN_TEST(dismount_stamps_current_time_not_dismount_time);
    RUN_TEST(mount_ors_node_me_without_masking);
    RUN_TEST(mount_defaults_the_step_longword_to_three);
    RUN_TEST(mount_writes_one_flag_byte);
    RUN_TEST(mount_chunk_geometry_from_dvtbl);
    RUN_TEST(mount_dismount_round_trip);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
