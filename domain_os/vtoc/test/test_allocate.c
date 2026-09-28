/*
 * vtoc/test/test_allocate.c - Unit tests for VTOC_$ALLOCATE (0x00E388AC)
 *
 * The real vtoc/allocate.c is #included below and driven through mocks for
 * DBUF, BAT, ML, the UID hash and the old/new VTOCE converter.  The tests
 * cover the behaviours the audit called out as wrong in the previous stub:
 *
 *   - the 0x20-byte location descriptor at arg1 is rewritten on EVERY exit
 *     path (0xE38F24-0xE38F72), including "volume not mounted"
 *   - arg1 supplies vol_idx (+0x1C) and the location hint (+0x04); arg2 is
 *     the VTOCE image whose UID lives at +4
 *   - the duplicate-UID detection in both formats returns 0x20007 and, in
 *     the new format, reports the existing VTOCE location
 *   - a successful allocation in either format publishes
 *     (block << 4) | entry and inserts it into the UID cache
 *
 * The kernel headers come first so that the Domain/OS definitions of
 * uid_t, boolean, true/false etc. win over the host's.
 */

#include "vtoc/vtoc_internal.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * Test harness
 * ================================================================ */

static int tests_failed = 0;
static int tests_run = 0;
static const char *current_test = "";

#define RUN_TEST(name) do {                     \
    current_test = #name;                       \
    tests_run++;                                \
    printf("  Running %s... ", #name);          \
    fflush(stdout);                             \
    if (test_##name() == 0) printf("PASSED\n"); \
} while (0)

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        printf("FAILED\n    %s at %s:%d\n", #cond, __FILE__,        \
               __LINE__);                                           \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

#define CHECK_EQ(expected, actual) do {                             \
    unsigned long e_ = (unsigned long)(expected);                   \
    unsigned long a_ = (unsigned long)(actual);                     \
    if (e_ != a_) {                                                 \
        printf("FAILED\n    %s: expected 0x%lx, got 0x%lx at %s:%d\n", \
               #actual, e_, a_, __FILE__, __LINE__);                \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

/* ================================================================
 * Mock globals the implementation links against
 * ================================================================ */

uint32_t ROUTE_$PORT = 0x11223344;
uint32_t NODE_$ME    = 0x00055555;

uint16_t PROC1_$CURRENT = 3;
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];

/* ================================================================
 * Mock callees
 * ================================================================ */

#define MAX_BUFS        8
#define BLOCK_SIZE      1024

static int ml_lock_count;
static int ml_unlock_count;

void ML_$LOCK(int16_t id)   { (void)id; ml_lock_count++; }
void ML_$UNLOCK(int16_t id) { (void)id; ml_unlock_count++; }

/* --- DBUF_$GET_BLOCK ------------------------------------------------- */

typedef struct {
    uint16_t    vol_idx;
    int32_t     block;
    uid_t      *uid;
    uint32_t    block_hint;
    /*
     * source-ve50: DBUF_$GET_BLOCK's parameters 5 and 6 are two separate
     * WORDS (0x00E3A5CE / 0x00E3A5D2), so record both.  Every VTOC_$ALLOCATE
     * call site passes block_type 0.
     */
    uint16_t    block_type;
    uint16_t    flags;
} get_block_call_t;

static get_block_call_t gb_calls[MAX_BUFS];
static int      gb_count;
/* Buffers handed out, in call order; NULL entries mean "reuse buf 0". */
static void    *gb_result[MAX_BUFS];
static status_$t gb_status[MAX_BUFS];

void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    int i = gb_count;

    if (i < MAX_BUFS) {
        gb_calls[i].vol_idx = vol_idx;
        gb_calls[i].block = block;
        gb_calls[i].uid = uid;
        gb_calls[i].block_hint = block_hint;
        gb_calls[i].block_type = block_type;
        gb_calls[i].flags = flags;
    }
    gb_count++;

    *status = (i < MAX_BUFS) ? gb_status[i] : status_$ok;
    return (i < MAX_BUFS) ? gb_result[i] : NULL;
}

/* --- DBUF_$SET_BUFF -------------------------------------------------- */

typedef struct {
    void       *buffer;
    uint16_t    flags;
} set_buff_call_t;

static set_buff_call_t sb_calls[MAX_BUFS];
static int sb_count;

void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    if (sb_count < MAX_BUFS) {
        sb_calls[sb_count].buffer = buffer;
        sb_calls[sb_count].flags = flags;
    }
    sb_count++;
    *status = status_$ok;
}

/* --- BAT ------------------------------------------------------------- */

static int      bat_allocate_count;
static uint32_t bat_allocate_result;

void BAT_$ALLOCATE(int16_t vol_idx, uint32_t hint, int16_t alloc_count,
                   int16_t use_reserved, uint32_t *blocks_out,
                   status_$t *status)
{
    (void)vol_idx; (void)hint; (void)alloc_count; (void)use_reserved;
    bat_allocate_count++;
    *blocks_out = bat_allocate_result;
    *status = status_$ok;
}

static int      alloc_vtoce_count;
static uint32_t alloc_vtoce_block;
static int8_t   alloc_vtoce_new_flag;
static void    *alloc_vtoce_buf;
static uint32_t alloc_vtoce_hint;

void *BAT_$ALLOC_VTOCE(int16_t vol_idx, uint32_t hint, uint32_t *block_out,
                       status_$t *status, int8_t *new_vtoce)
{
    (void)vol_idx;
    alloc_vtoce_count++;
    alloc_vtoce_hint = hint;
    *block_out = alloc_vtoce_block;
    *new_vtoce = alloc_vtoce_new_flag;
    *status = status_$ok;
    return alloc_vtoce_buf;
}

/* --- VTOC helpers ---------------------------------------------------- */

static uint16_t hash_bucket;
static uint32_t hash_block;
static int      hash_count;

void vtoc_$hash_uid(uid_t *uid, short vol_idx, uint16_t *bucket_idx,
                    uint32_t *block, status_$t *status)
{
    (void)uid; (void)vol_idx;
    hash_count++;
    *bucket_idx = hash_bucket;
    *block = hash_block;
    *status = status_$ok;
}

static int      cache_insert_count;
static uid_t    cache_insert_uid;
static int16_t  cache_insert_vol;
static uint32_t cache_insert_loc;

void vtoc_$uid_cache_insert(uid_t *uid, int16_t vol_idx, uint32_t block_info)
{
    cache_insert_count++;
    cache_insert_uid = *uid;
    cache_insert_vol = vol_idx;
    cache_insert_loc = block_info;
}

static int    new_to_old_count;
static void  *new_to_old_src;
static void  *new_to_old_dst;
static char   new_to_old_flag_value;
static char  *new_to_old_flag_ptr;

void VTOCE_$NEW_TO_OLD(void *new_vtoce_ptr, char *flags, void *old_vtoce_ptr)
{
    new_to_old_count++;
    new_to_old_src = new_vtoce_ptr;
    new_to_old_dst = old_vtoce_ptr;
    new_to_old_flag_ptr = flags;
    new_to_old_flag_value = *flags;
}

/* ================================================================
 * Code under test
 * ================================================================ */

#include "../vtoc_data.c"
#include "../allocate.c"

/* ================================================================
 * Fixtures
 * ================================================================ */

#define TEST_VOL        2
#define TEST_UID_HIGH   0x0A0B0C0Du
#define TEST_UID_LOW    0x10203040u

/* 0x150-byte VTOCE image handed to VTOC_$ALLOCATE (arg2) */
static uint8_t new_vtoce_img[VTOCE_NEW_SIZE];
static vtoc_$lookup_req_t loc;
static uint8_t buf_a[BLOCK_SIZE];
static uint8_t buf_b[BLOCK_SIZE];

static void reset_all(uint32_t block_hint)
{
    memset(&vtoc_$data, 0, sizeof(vtoc_$data));
    memset(new_vtoce_img, 0, sizeof(new_vtoce_img));
    memset(&loc, 0xAA, sizeof(loc));
    memset(buf_a, 0, sizeof(buf_a));
    memset(buf_b, 0, sizeof(buf_b));
    memset(PROC1_$TYPE, 0, sizeof(PROC1_$TYPE));
    memset(gb_calls, 0, sizeof(gb_calls));
    memset(gb_result, 0, sizeof(gb_result));
    memset(gb_status, 0, sizeof(gb_status));
    memset(sb_calls, 0, sizeof(sb_calls));

    ml_lock_count = ml_unlock_count = 0;
    gb_count = sb_count = 0;
    bat_allocate_count = 0;
    bat_allocate_result = 0;
    alloc_vtoce_count = 0;
    alloc_vtoce_block = 0;
    alloc_vtoce_new_flag = 0;
    alloc_vtoce_buf = NULL;
    alloc_vtoce_hint = 0;
    hash_count = 0;
    hash_bucket = 0;
    hash_block = 0;
    cache_insert_count = 0;
    cache_insert_loc = 0;
    new_to_old_count = 0;
    new_to_old_flag_value = (char)0xEE;

    /* Volume 2 is mounted; the caller's descriptor names it. */
    vtoc_$data.mounted[TEST_VOL] = (int8_t)0xFF;
    /* Per-volume word at OS_DISK_DATA[vol*2 - 2], copied to loc+2 on exit */
    *(uint16_t *)(OS_DISK_DATA + TEST_VOL * 2 - 2) = 0x1357;

    loc.vol_idx = TEST_VOL;
    loc.block_hint = block_hint;
    loc.uid.high = 0xDEADBEEFu;      /* must survive untouched */
    loc.uid.low  = 0xFEEDFACEu;

    ((vtoce_$hdr_t *)new_vtoce_img)->uid.high = TEST_UID_HIGH;
    ((vtoce_$hdr_t *)new_vtoce_img)->uid.low  = TEST_UID_LOW;
}

/* Common assertions for the unconditional descriptor rewrite at 0xE38F24 */
static int check_descriptor(uint32_t expect_loc)
{
    CHECK_EQ(0x00011357u, loc.flags);       /* word at +2, nibble at byte 1 */
    CHECK_EQ(expect_loc, loc.block_hint);
    CHECK_EQ(0xDEADBEEFu, loc.uid.high);    /* untouched by 0xE38F24-0xE38F72 */
    CHECK_EQ(0xFEEDFACEu, loc.uid.low);
    CHECK_EQ(ROUTE_$PORT, loc.port);
    CHECK_EQ(NODE_$ME, loc.node);
    CHECK_EQ(0u, loc.reserved_18);
    CHECK_EQ(TEST_VOL, loc.vol_idx);
    CHECK_EQ(0x41, loc.flags_1d);           /* bit 6 set, low nibble = 1 */
    CHECK_EQ(0u, loc.reserved_1e);
    CHECK_EQ(1, ml_lock_count);
    CHECK_EQ(1, ml_unlock_count);
    return 0;
}

/* ================================================================
 * Tests
 * ================================================================ */

/*
 * 0xE388E6: an unmounted volume fails immediately, but the descriptor is
 * still rewritten at 0xE38F24 with the caller's original hint.
 */
static int test_not_mounted_still_rewrites_descriptor(void)
{
    status_$t status = 0x5A5A5A5Au;

    reset_all(0x00001234u);
    vtoc_$data.mounted[TEST_VOL] = 0;       /* not mounted */

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ(status_$VTOC_not_mounted, status);
    CHECK_EQ(0, hash_count);
    CHECK_EQ(0, gb_count);
    if (check_descriptor(0x00001234u)) return 1;
    return 0;
}

/*
 * 0xE388CE: the caller's VTOCE is marked "in use" before anything else,
 * even on a failing path.
 */
static int test_marks_input_vtoce_in_use(void)
{
    status_$t status = status_$ok;
    vtoce_$hdr_t *hdr = (vtoce_$hdr_t *)new_vtoce_img;

    reset_all(0);
    vtoc_$data.mounted[TEST_VOL] = 0;
    hdr->status = 0x0102;

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ((uint16_t)0x8102, (uint16_t)hdr->status);
    return 0;
}

/*
 * Old format (0xE38CD4): entry 0 is in use and carries the same UID.
 * 0xE38D44 sets 0x20007 and 0xE38EC0 releases the block with code 8.
 */
static int test_old_format_duplicate_uid(void)
{
    status_$t status = status_$ok;
    vtoc_$old_block_t *blk = (vtoc_$old_block_t *)buf_a;

    reset_all(0x00000042u);
    vtoc_$data.format[TEST_VOL] = 0;        /* >= 0 selects the old format */
    hash_block = 77;
    gb_result[0] = buf_a;

    blk->entries[0].hdr.status = VTOCE_STATUS_IN_USE;
    blk->entries[0].hdr.uid.high = TEST_UID_HIGH;
    blk->entries[0].hdr.uid.low = TEST_UID_LOW;

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ(status_$vtoc_duplicate_uid, status);
    CHECK_EQ(1, gb_count);
    CHECK_EQ(77, gb_calls[0].block);
    CHECK(gb_calls[0].uid == &VTOC_$UID);
    CHECK_EQ(0u, gb_calls[0].flags);
    CHECK_EQ(0u, gb_calls[0].block_type);   /* the (0x16,A6) word */
    /* one release, code 8, of the block we were handed */
    CHECK_EQ(1, sb_count);
    CHECK(sb_calls[0].buffer == buf_a);
    CHECK_EQ(8, sb_calls[0].flags);
    CHECK_EQ(0, cache_insert_count);
    /* the hint is unchanged on this path */
    if (check_descriptor(0x00000042u)) return 1;
    return 0;
}

/*
 * Old format success: entry 1 is free (entry 0 in use with another UID).
 * 0xE38E1E builds (block << 4) | entry and 0xE38EAA caches it.
 */
static int test_old_format_allocates_free_entry(void)
{
    status_$t status = 0x99999999u;
    vtoc_$old_block_t *blk = (vtoc_$old_block_t *)buf_a;

    reset_all(0x0000000Fu);
    vtoc_$data.format[TEST_VOL] = 0;
    hash_block = 0x30;
    gb_result[0] = buf_a;

    blk->entries[0].hdr.status = VTOCE_STATUS_IN_USE;
    blk->entries[0].hdr.uid.high = 0x11111111u;
    blk->entries[0].hdr.uid.low = 0x22222222u;
    blk->entries[1].hdr.status = 0;         /* free */
    /* something in the region 0x40..0xCB that must be zeroed */
    blk->entries[1].rest[0x40 - 0x0C] = 0x5A;
    blk->entries[1].rest[0xC8 - 0x0C] = 0x5A;

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ(status_$ok, status);
    /* 0xE38E4A: bytes 0x40..0xCB of the chosen entry are cleared */
    CHECK_EQ(0, blk->entries[1].rest[0x40 - 0x0C]);
    CHECK_EQ(0, blk->entries[1].rest[0xC8 - 0x0C]);
    /* 0xE38E5A: converter writes into entry 1, flag cell reads 0 */
    CHECK_EQ(1, new_to_old_count);
    CHECK(new_to_old_src == (void *)new_vtoce_img);
    CHECK(new_to_old_dst == (void *)&blk->entries[1]);
    CHECK_EQ(0, new_to_old_flag_value);
    CHECK(new_to_old_flag_ptr == &vtoc_$new_to_old_flags_00e38f7e);
    /* 0xE38E6E: block released dirty (9), no writeback */
    CHECK_EQ(1, sb_count);
    CHECK(sb_calls[0].buffer == buf_a);
    CHECK_EQ(9, sb_calls[0].flags);
    /* (0x30 << 4) | 1 */
    CHECK_EQ(0x00000301u, cache_insert_loc);
    CHECK_EQ(1, cache_insert_count);
    CHECK_EQ(TEST_VOL, cache_insert_vol);
    CHECK_EQ(TEST_UID_HIGH, cache_insert_uid.high);
    CHECK_EQ(TEST_UID_LOW, cache_insert_uid.low);
    if (check_descriptor(0x00000301u)) return 1;
    return 0;
}

/*
 * New format (0xE3893A) with PROC1_$TYPE[current] == 9: the bucket chain
 * walk at 0xE38992 finds the UID and reports its existing location.
 */
static int test_new_format_duplicate_uid_reports_location(void)
{
    status_$t status = status_$ok;
    vtoc_$bkt_block_t *blk = (vtoc_$bkt_block_t *)buf_a;

    reset_all(0x00000007u);
    vtoc_$data.format[TEST_VOL] = (int8_t)0x80;  /* < 0 = new format */
    PROC1_$TYPE[PROC1_$CURRENT] = 9;
    hash_bucket = 1;
    hash_block = 0x50;
    gb_result[0] = buf_a;

    /* bucket 1, slot 3 already holds this UID at location 0x00004321 */
    blk->buckets[1].slots[0].block_info = 0x1000;
    blk->buckets[1].slots[1].block_info = 0x1001;
    blk->buckets[1].slots[2].block_info = 0x1002;
    blk->buckets[1].slots[3].uid.high = TEST_UID_HIGH;
    blk->buckets[1].slots[3].uid.low = TEST_UID_LOW;
    blk->buckets[1].slots[3].block_info = 0x00004321u;

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ(status_$vtoc_duplicate_uid, status);
    CHECK_EQ(1, gb_count);
    CHECK(gb_calls[0].uid == &VTOC_BKT_$UID);
    CHECK_EQ(0x50, gb_calls[0].block);
    /* 0xE38EDC releases the hash bucket buffer with bkt_dirty == 8 */
    CHECK_EQ(1, sb_count);
    CHECK(sb_calls[0].buffer == buf_a);
    CHECK_EQ(8, sb_calls[0].flags);
    CHECK_EQ(0, cache_insert_count);
    /* 0xE389D6 copied the existing location into the descriptor */
    if (check_descriptor(0x00004321u)) return 1;
    return 0;
}

/*
 * New format success (0xE38B90): a free bucket slot plus a VTOCE block from
 * BAT_$ALLOC_VTOCE.  Checks the 0x90-byte copy, the zero fill from 0x40,
 * the type byte, the published slot and the buffer release codes.
 */
static int test_new_format_allocates_and_publishes_slot(void)
{
    status_$t status = 0x99999999u;
    vtoc_$bkt_block_t *bkt = (vtoc_$bkt_block_t *)buf_a;
    vtoc_$vtoce_block_t *vblk = (vtoc_$vtoce_block_t *)buf_b;
    int i;

    reset_all(0x00001234u);
    vtoc_$data.format[TEST_VOL] = (int8_t)0x80;
    PROC1_$TYPE[PROC1_$CURRENT] = 5;    /* not 9: no chain walk */
    hash_bucket = 0;
    hash_block = 0x50;
    gb_result[0] = buf_a;

    alloc_vtoce_block = 0x200;
    alloc_vtoce_new_flag = 0;           /* existing block -> release dirty (9) */
    alloc_vtoce_buf = buf_b;

    /* entry 0 in use, entry 1 free */
    vblk->entries[0].hdr.status = VTOCE_STATUS_IN_USE;
    vblk->entries[1].hdr.status = 0;
    vblk->entries[1].rest[0x40 - 0x0C] = 0x5A;
    vblk->entries[1].rest[0x14C - 0x0C] = 0x5A;

    /* recognisable payload in the 0x90 bytes that get copied */
    for (i = 0x0C; i < 0x90; i++) {
        new_vtoce_img[i] = (uint8_t)(i + 1);
    }

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(1, alloc_vtoce_count);
    /* 0xE38B90: hint is the caller's location shifted right by 4 */
    CHECK_EQ(0x123u, alloc_vtoce_hint);

    /* 0xE38C1E: type byte forced to 1 in the caller's image, then copied */
    CHECK_EQ(1, ((vtoce_$hdr_t *)new_vtoce_img)->type_mode);
    CHECK_EQ(1, vblk->entries[1].hdr.type_mode);
    CHECK_EQ((uint16_t)0x8000, (uint16_t)vblk->entries[1].hdr.status);
    CHECK_EQ(TEST_UID_HIGH, vblk->entries[1].hdr.uid.high);
    CHECK_EQ(TEST_UID_LOW, vblk->entries[1].hdr.uid.low);
    /* last byte of the 0x24-longword copy */
    CHECK_EQ(0x90, vblk->entries[1].rest[0x8F - 0x0C]);
    /* 0xE38C04: 0x40..0x14F cleared before the copy; 0x40 is then overwritten
     * by the copy, 0x14C is not */
    CHECK_EQ(0x41, vblk->entries[1].rest[0x40 - 0x0C]);
    CHECK_EQ(0, vblk->entries[1].rest[0x14C - 0x0C]);

    /* 0xE38C60: (0x200 << 4) | 1 */
    CHECK_EQ(0x00002001u, cache_insert_loc);

    /* 0xE38C84: the free slot 0 of bucket 0 now names the new VTOCE */
    CHECK_EQ(TEST_UID_HIGH, bkt->buckets[0].slots[0].uid.high);
    CHECK_EQ(TEST_UID_LOW, bkt->buckets[0].slots[0].uid.low);
    CHECK_EQ(0x00002001u, bkt->buckets[0].slots[0].block_info);

    /* 0xE38C40 releases the VTOCE block dirty (9); 0xE38CB0 releases the
     * bucket block with bkt_dirty | 1 == 9 */
    CHECK_EQ(2, sb_count);
    CHECK(sb_calls[0].buffer == buf_b);
    CHECK_EQ(9, sb_calls[0].flags);
    CHECK(sb_calls[1].buffer == buf_a);
    CHECK_EQ(9, sb_calls[1].flags);

    CHECK_EQ(1, cache_insert_count);
    if (check_descriptor(0x00002001u)) return 1;
    return 0;
}

/*
 * A freshly allocated VTOCE block (BAT_$ALLOC_VTOCE reports 0xFF) must be
 * written back immediately: 0xE38C2E selects code 0xB instead of 9.
 */
static int test_new_vtoce_block_forces_writeback(void)
{
    status_$t status = status_$ok;
    vtoc_$vtoce_block_t *vblk = (vtoc_$vtoce_block_t *)buf_b;

    reset_all(0);
    vtoc_$data.format[TEST_VOL] = (int8_t)0x80;
    hash_bucket = 0;
    hash_block = 0x50;
    gb_result[0] = buf_a;
    alloc_vtoce_block = 0x300;
    alloc_vtoce_new_flag = (int8_t)0xFF;
    alloc_vtoce_buf = buf_b;
    vblk->entries[0].hdr.status = 0;    /* free */

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(2, sb_count);
    CHECK(sb_calls[0].buffer == buf_b);
    CHECK_EQ(0x0B, sb_calls[0].flags);
    CHECK_EQ(0x00003000u, cache_insert_loc);
    return 0;
}

/*
 * 0xE38BEA: no free entry in the VTOCE block handed back by BAT_$ALLOC_VTOCE
 * yields 0x80020002 and releases both buffers (0xE38EC0 then 0xE38EDC).
 */
static int test_no_free_vtoce_in_block(void)
{
    status_$t status = status_$ok;
    vtoc_$vtoce_block_t *vblk = (vtoc_$vtoce_block_t *)buf_b;
    int i;

    reset_all(0);
    vtoc_$data.format[TEST_VOL] = (int8_t)0x80;
    hash_bucket = 0;
    hash_block = 0x50;
    gb_result[0] = buf_a;
    alloc_vtoce_block = 0x400;
    alloc_vtoce_buf = buf_b;
    for (i = 0; i < VTOCE_NEW_ENTRIES_PER_BLOCK; i++) {
        vblk->entries[i].hdr.status = VTOCE_STATUS_IN_USE;
    }

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ((uint32_t)status_$VTOC_uid_mismatch, (uint32_t)status);   /* status_$t is int32_t: compare as 32-bit */
    CHECK_EQ(2, sb_count);
    CHECK(sb_calls[0].buffer == buf_b);
    CHECK_EQ(8, sb_calls[0].flags);
    CHECK(sb_calls[1].buffer == buf_a);
    CHECK_EQ(8, sb_calls[1].flags);
    CHECK_EQ(0, cache_insert_count);
    return 0;
}

/*
 * 0xE38A98-0xE38B8C: when the hash bucket is full and has no successor, the
 * volume's spare bucket block is used, the chain is linked, the old bucket
 * buffer becomes prev_buf and the scan restarts in the new bucket.
 */
static int test_full_bucket_chains_to_volume_spare(void)
{
    status_$t status = status_$ok;
    vtoc_$bkt_block_t *first = (vtoc_$bkt_block_t *)buf_a;
    vtoc_$bkt_block_t *second = (vtoc_$bkt_block_t *)buf_b;
    vtoc_$vol_t *vol;
    uint8_t vtoce_blk[BLOCK_SIZE];
    int k;

    reset_all(0);
    memset(vtoce_blk, 0, sizeof(vtoce_blk));
    vtoc_$data.format[TEST_VOL] = (int8_t)0x80;
    hash_bucket = 0;
    hash_block = 0x50;
    gb_result[0] = buf_a;
    gb_result[1] = buf_b;

    vol = VTOC_VOL(TEST_VOL);
    vol->cur_bkt_block = 0x900;
    vol->cur_bkt_idx = 2;

    /* every slot of bucket 0 in the first block is taken */
    for (k = 0; k <= 0x13; k++) {
        first->buckets[0].slots[k].block_info = 0x7000u + k;
    }

    alloc_vtoce_block = 0x500;
    alloc_vtoce_buf = vtoce_blk;
    ((vtoc_$vtoce_block_t *)vtoce_blk)->entries[0].hdr.status = 0;

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ(status_$ok, status);
    /* 0xE38AF8: the full bucket now points at the volume's spare */
    CHECK_EQ(0x900u, first->buckets[0].next_bucket);
    CHECK_EQ(2, first->buckets[0].next_bkt_idx);
    /* 0xE38AEA: the volume's cursor advanced but did not wrap (2 -> 3) */
    CHECK_EQ(3, vol->cur_bkt_idx);
    CHECK_EQ(0x900u, vol->cur_bkt_block);
    CHECK_EQ(0, bat_allocate_count);
    /* second DBUF_$GET_BLOCK for the spare block, flags 0 (not new) */
    CHECK_EQ(2, gb_count);
    CHECK_EQ(0x900, gb_calls[1].block);
    CHECK_EQ(0u, gb_calls[1].flags);
    /* slot 0 of bucket 2 in the new block got the entry */
    CHECK_EQ(0x00005000u, second->buckets[2].slots[0].block_info);
    /* 0xE38EAA / 0xE38E92: prev_buf (the first bucket block) released dirty */
    CHECK_EQ(3, sb_count);
    CHECK(sb_calls[0].buffer == vtoce_blk);     /* VTOCE block, 9 */
    CHECK(sb_calls[1].buffer == buf_b);         /* new bucket block, 9 */
    CHECK_EQ(9, sb_calls[1].flags);
    CHECK(sb_calls[2].buffer == buf_a);         /* prev_buf, 9 */
    CHECK_EQ(9, sb_calls[2].flags);
    return 0;
}

/*
 * 0xE38AA2: with no spare bucket block on the volume, BAT_$ALLOCATE supplies
 * one, the block is zeroed and stamped (0xE38B70-0xE38B86) and the whole
 * bucket dirty code is promoted to writeback.
 */
static int test_full_bucket_allocates_new_block(void)
{
    status_$t status = status_$ok;
    vtoc_$bkt_block_t *second = (vtoc_$bkt_block_t *)buf_b;
    vtoc_$vol_t *vol;
    uint8_t vtoce_blk[BLOCK_SIZE];
    int k;

    reset_all(0);
    memset(vtoce_blk, 0, sizeof(vtoce_blk));
    vtoc_$data.format[TEST_VOL] = (int8_t)0x80;
    hash_bucket = 0;
    hash_block = 0x50;
    gb_result[0] = buf_a;
    gb_result[1] = buf_b;
    bat_allocate_result = 0xA00;

    vol = VTOC_VOL(TEST_VOL);
    vol->cur_bkt_block = 0;
    vol->cur_bkt_idx = 0;
    vol->blocks_added = 5;

    for (k = 0; k <= 0x13; k++) {
        ((vtoc_$bkt_block_t *)buf_a)->buckets[0].slots[k].block_info = 1;
    }
    /* garbage that the zero fill must remove */
    memset(buf_b, 0xEE, BLOCK_SIZE);

    alloc_vtoce_block = 0x600;
    alloc_vtoce_buf = vtoce_blk;
    ((vtoc_$vtoce_block_t *)vtoce_blk)->entries[0].hdr.status = 0;

    VTOC_$ALLOCATE(&loc, new_vtoce_img, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(1, bat_allocate_count);
    CHECK_EQ(6u, vol->blocks_added);
    CHECK_EQ(0xA00u, vol->cur_bkt_block);
    CHECK_EQ(1, vol->cur_bkt_idx);
    /* 0xE38B36: the block is fetched with flag 0x10 (no read from disk) */
    CHECK_EQ(2, gb_count);
    CHECK_EQ(0x10u, gb_calls[1].flags);     /* the (0x18,A6) word */
    CHECK_EQ(0u, gb_calls[1].block_type);   /* the (0x16,A6) word */
    /* 0xE38B70-0xE38B86: 254 longwords cleared, then magic + self block.
     * Slot 0 of bucket 0 is where the new entry lands, so check a slot the
     * allocation does not touch. */
    CHECK_EQ(0u, second->buckets[0].slots[1].uid.high);
    CHECK_EQ(0u, second->buckets[3].slots[0].block_info);
    CHECK_EQ(VTOC_BKT_BLOCK_MAGIC, second->magic);
    CHECK_EQ(0xA00u, second->self_block);
    /* the new entry went into bucket 0 (vol->cur_bkt_idx was 0), slot 0 */
    CHECK_EQ(0x00006000u, second->buckets[0].slots[0].block_info);
    /* 0xE38AD6 promoted bkt_dirty to 0xB, so the final release is 0xB|1 */
    CHECK_EQ(3, sb_count);
    CHECK(sb_calls[1].buffer == buf_b);
    CHECK_EQ(0x0B, sb_calls[1].flags);
    return 0;
}

int main(void)
{
    printf("=== VTOC_$ALLOCATE tests ===\n");

    RUN_TEST(not_mounted_still_rewrites_descriptor);
    RUN_TEST(marks_input_vtoce_in_use);
    RUN_TEST(old_format_duplicate_uid);
    RUN_TEST(old_format_allocates_free_entry);
    RUN_TEST(new_format_duplicate_uid_reports_location);
    RUN_TEST(new_format_allocates_and_publishes_slot);
    RUN_TEST(new_vtoce_block_forces_writeback);
    RUN_TEST(no_free_vtoce_in_block);
    RUN_TEST(full_bucket_chains_to_volume_spare);
    RUN_TEST(full_bucket_allocates_new_block);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
