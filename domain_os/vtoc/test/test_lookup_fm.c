/*
 * vtoc/test/test_lookup_fm.c - unit tests for VTOCE_$LOOKUP_FM (0x00E39A04)
 * and its nested walk vtoc_$fm_traverse (0x00E397D0)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "vtoc/vtoc_internal.h"

vtoc_$data_t vtoc_$data;
uid_t VTOC_$UID = { 0x1739C, 0 };

/* ---- disk: blocks 0..15, 1 KB each ---- */
static uint32_t disk[16][256];
static int n_get, n_set, n_alloc_fm, n_alloc, n_lock, n_unlock;
static int32_t get_block[8];
static uint16_t get_type[8], get_flags[8];
static uint32_t get_hint[8];
static uint16_t set_flags[8];
static status_$t set_status_val;
static uint32_t alloc_next, alloc_hint;
static status_$t get_status_val, alloc_status_val;

void ML_$LOCK(int16_t id) { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    (void)vol_idx; (void)uid;
    get_block[n_get] = block; get_hint[n_get] = block_hint;
    get_type[n_get] = block_type; get_flags[n_get] = flags;
    n_get++;
    *status = get_status_val;
    return disk[block];
}
void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    (void)buffer; set_flags[n_set++] = flags; *status = set_status_val;
}
uint32_t BAT_$ALLOC_FM(int16_t vol_idx, status_$t *status)
{
    (void)vol_idx; n_alloc_fm++; *status = alloc_status_val; return alloc_next++;
}
void BAT_$ALLOCATE(int16_t vol_idx, uint32_t hint, int16_t alloc_count,
                   int16_t use_reserved, uint32_t *blocks_out, status_$t *status)
{
    (void)vol_idx; (void)alloc_count; (void)use_reserved;
    n_alloc++; alloc_hint = hint; *blocks_out = alloc_next++; *status = alloc_status_val;
}

#include "../lookup_fm.c"

static vtoc_$lookup_req_t loc;
static uint32_t fm_loc, alloc_count;
static status_$t st;

/* new-format entry 2 of VTOCE block 3: roots at +2*0x150+0xCC */
static uint32_t *roots(void)
{
    return (uint32_t *)(void *)((uint8_t *)disk[3] + 2 * 0x150 + 0xCC);
}

static void reset_state(void)
{
    memset(disk, 0, sizeof(disk));
    memset(&vtoc_$data, 0, sizeof(vtoc_$data));
    memset(&loc, 0, sizeof(loc));
    n_get = n_set = n_alloc_fm = n_alloc = n_lock = n_unlock = 0;
    get_status_val = set_status_val = alloc_status_val = 0;
    alloc_next = 10;
    loc.block_hint = (3u << 4) | 2;
    loc.vol_idx = 1;
    vtoc_$data.mounted[1] = (int8_t)0xFF;
    vtoc_$data.format[1] = (int8_t)0xFF;
    fm_loc = 0xFFFFFFFF;
    alloc_count = 99;
    st = -1;
}

static void test_not_mounted(void)
{
    vtoc_$data.mounted[1] = 0;
    VTOCE_$LOOKUP_FM(&loc, 1, 0, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0x20001, st);
    ASSERT_EQ(0, n_get);
    ASSERT_EQ(1, n_unlock);
    ASSERT_EQ(0xFFFFFFFF, fm_loc);
}

static void test_segment_zero_is_the_vtoce(void)
{
    VTOCE_$LOOKUP_FM(&loc, 0, 0, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, n_get);
    ASSERT_EQ(3, get_block[0]);
    ASSERT_EQ(3, get_hint[0]);
    ASSERT_EQ(0, alloc_count);
    ASSERT_EQ(8, set_flags[0]);
    /* keeps fm_loc's low nibble under the block, then slot 2 */
    ASSERT_EQ((3u << 4) | 2, fm_loc);
}

static void test_direct_existing(void)
{
    roots()[0] = 7;
    VTOCE_$LOOKUP_FM(&loc, 5, 0, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, n_get);
    ASSERT_EQ((7u << 4) | 4, fm_loc);
    ASSERT_EQ(8, set_flags[0]);
}

static void test_direct_missing_no_alloc(void)
{
    VTOCE_$LOOKUP_FM(&loc, 1, 0, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0x20003, st);
    ASSERT_EQ(1, n_set);
    ASSERT_EQ(8, set_flags[0]);
    /* the status is not cleared, fm_loc still merged: block 3, slot 0 */
    ASSERT_EQ((3u << 4) | 0, fm_loc);
}

static void test_direct_missing_allocates(void)
{
    VTOCE_$LOOKUP_FM(&loc, 1, (int8_t)0xFF, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, n_alloc_fm);
    ASSERT_EQ(1, alloc_count);
    ASSERT_EQ(10, roots()[0]);
    ASSERT_EQ(2, n_get);
    ASSERT_EQ(10, get_block[1]);
    ASSERT_EQ(0x20, get_hint[1]);
    ASSERT_EQ(1, get_type[1]);
    ASSERT_EQ(0x10, get_flags[1]);
    ASSERT_EQ(0xB, set_flags[0]);       /* the new map block */
    ASSERT_EQ(9, set_flags[1]);         /* the VTOCE: 0xB & ~2 */
    ASSERT_EQ((10u << 4) | 0, fm_loc);
}

static void test_read_only_volume(void)
{
    vtoc_$data.cach_wp_flag[0] = (int8_t)0xFF;
    VTOCE_$LOOKUP_FM(&loc, 1, (int8_t)0xFF, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ((1u << 4) | 0xF, fm_loc);
}

static void test_indirect_existing(void)
{
    /* segment 9 + 8*3 + 5 = 38: indirect entry 3, slot 5 */
    roots()[1] = 4;
    disk[4][3] = 6;
    VTOCE_$LOOKUP_FM(&loc, 38, 0, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, n_get);
    ASSERT_EQ(4, get_block[1]);
    ASSERT_EQ(0x120, get_hint[1]);
    ASSERT_EQ(2, get_type[1]);
    ASSERT_EQ(0, get_flags[1]);
    ASSERT_EQ(8, set_flags[0]);
    ASSERT_EQ((6u << 4) | 5, fm_loc);
}

static void test_indirect_alloc_uses_neighbour_hint(void)
{
    roots()[1] = 4;
    disk[4][1] = 12;                    /* entry 1 used, 2 empty */
    VTOCE_$LOOKUP_FM(&loc, 9 + 8 * 3, (int8_t)0xFF, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, n_alloc);
    ASSERT_EQ(12, alloc_hint);
    ASSERT_EQ(10, disk[4][3]);
    ASSERT_EQ(3, n_get);
    ASSERT_EQ(0x120 + 0x300, get_hint[2]);
    ASSERT_EQ(3, n_set);
    ASSERT_EQ(0xB, set_flags[0]);       /* the new map */
    ASSERT_EQ(9, set_flags[1]);         /* the indirect block: 8 | (0xB & ~2) */
    ASSERT_EQ(8, set_flags[2]);         /* the VTOCE: 8 & ~2 */
    ASSERT_EQ((10u << 4) | 0, fm_loc);
}

static void test_old_format_roots(void)
{
    vtoc_$data.format[1] = 0;
    *(uint32_t *)(void *)((uint8_t *)disk[3] + 2 * 0xCC + 0xC4) = 9;
    VTOCE_$LOOKUP_FM(&loc, 2, 0, &fm_loc, &alloc_count, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ((9u << 4) | 1, fm_loc);
}

int main(void)
{
    printf("VTOCE_$LOOKUP_FM tests\n");
    RUN_TEST(not_mounted);
    RUN_TEST(segment_zero_is_the_vtoce);
    RUN_TEST(direct_existing);
    RUN_TEST(direct_missing_no_alloc);
    RUN_TEST(direct_missing_allocates);
    RUN_TEST(read_only_volume);
    RUN_TEST(indirect_existing);
    RUN_TEST(indirect_alloc_uses_neighbour_hint);
    RUN_TEST(old_format_roots);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
