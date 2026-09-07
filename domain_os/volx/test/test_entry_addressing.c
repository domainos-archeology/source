/*
 * volx/test/test_entry_addressing.c - VOLX mount table addressing
 *
 * The image reaches a VOLX entry through a pointer biased by one entry:
 * `A5 + vol_idx * 0x20` is the address just PAST entry vol_idx, and every
 * field is read at a negative displacement from it (see volx_internal.h for
 * the per-instruction derivation).  The indices are therefore 1-based and
 * the six entries fill the map segment `D  E82604  VOLX_  size = C0` exactly.
 *
 * Covered:
 *   - VOLX_$ENTRY(1) is the first byte of the table and VOLX_$ENTRY(6) ends
 *     on the segment's last byte (the regression this file exists for: the
 *     old unbiased macro put index 6 one entry past the end, inside DISK_)
 *   - the biased target arithmetic `VOLX_$TABLE_ADDR + idx*0x20 - 0x20`
 *     resolves, through an ARCH_HOST arena, to the same entry VOLX_$ENTRY()
 *     names (0x00E6B0DE, 0x00E6B2C0, 0x00E6B6CA)
 *   - each field's negative displacement off the biased pointer matches its
 *     struct offset (0x00E6B0E6/0x00E6B0EC/0x00E6B0F2/0x00E6B0F8,
 *     0x00E6B2C8..0x00E6B2F4)
 *   - FIND_VOLX scans indices 1..6 and no further (0x00E6B0DA/0x00E6B0DC/
 *     0x00E6B0108)
 *   - VOLX_$REC_ENTRY, VOLX_$GET_UIDS and VOLX_$GET_INFO all land on the
 *     same 1-based entry
 */

#include <stdio.h>
#include <string.h>

#include "volx/volx_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Mocks
 * ============================================================================ */

static status_$t bat_n_free_status;
static uint16_t  bat_n_free_idx;
static int       bat_n_free_calls;

void BAT_$N_FREE(uint16_t *vol_idx_ptr, uint32_t *free_out,
                 uint32_t *total_out, status_$t *status)
{
    bat_n_free_calls++;
    bat_n_free_idx = *vol_idx_ptr;
    *free_out = 0x11112222u;
    *total_out = 0x33334444u;
    *status = bat_n_free_status;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../volx_data.c"
#include "../find_volx.c"
#include "../rec_entry.c"
#include "../get_uids.c"
#include "../get_info.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

/* The size of the VOLX_ data segment in the SR10.2 SAU2 link map. */
#define VOLX_TABLE_BYTES 0xC0

/*
 * Ground truth, independent of VOLX_$ENTRY(): entry idx starts at
 * table + (idx - 1) * 0x20, because base + idx * 0x20 is the address just
 * past it and every field is read at a negative displacement from there.
 */
static volx_$entry_t *raw_entry(int16_t idx)
{
    return (volx_$entry_t *)((uint8_t *)VOLX_$TABLE
                             + ((int32_t)idx - 1) * 0x20);
}

static void clear_table(void)
{
    memset(VOLX_$TABLE, 0, VOLX_TABLE_BYTES);
}

/* Fill entry `idx` with recognisable values. */
static void fill_entry(int16_t idx)
{
    volx_$entry_t *e = raw_entry(idx);

    e->dir_uid.high    = 0xD1000000u | (uint32_t)idx;
    e->dir_uid.low     = 0xD2000000u | (uint32_t)idx;
    e->lv_uid.high     = 0xE1000000u | (uint32_t)idx;
    e->lv_uid.low      = 0xE2000000u | (uint32_t)idx;
    e->parent_uid.high = 0xF1000000u | (uint32_t)idx;
    e->parent_uid.low  = 0xF2000000u | (uint32_t)idx;
    e->dev             = (int16_t)(0x0100 + idx);
    e->bus             = (int16_t)(0x0200 + idx);
    e->ctlr            = (int16_t)(0x0300 + idx);
    e->lv_num          = (int16_t)(0x0400 + idx);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/*
 * The table is one entry per volume index 1..6 and nothing more.  With the
 * old unbiased macro (&base[idx]) index 6 started at base + 0xC0, the first
 * byte of the DISK_ segment at 0xE826C4.
 */
TEST(table_extent_is_the_map_segment)
{
    ASSERT_EQ(0x20, sizeof(volx_$entry_t));
    ASSERT_EQ(VOLX_TABLE_BYTES, sizeof(volx_$entry_t) * VOLX_MAX_VOLUMES);

    /* entry 1 is the first byte of the segment */
    ASSERT_EQ((uintptr_t)VOLX_$TABLE, (uintptr_t)VOLX_$ENTRY(1));
    for (int16_t i = 1; i <= VOLX_MAX_VOLUMES; i++) {
        ASSERT_EQ((uintptr_t)raw_entry(i), (uintptr_t)VOLX_$ENTRY(i));
    }

    /* entry 6 is the last entry, ending on the segment's last byte */
    ASSERT_EQ((uintptr_t)VOLX_$TABLE + VOLX_TABLE_BYTES - 0x20,
              (uintptr_t)VOLX_$ENTRY(VOLX_MAX_VOLUMES));
    ASSERT_EQ((uintptr_t)VOLX_$TABLE + VOLX_TABLE_BYTES,
              (uintptr_t)(VOLX_$ENTRY(VOLX_MAX_VOLUMES) + 1));
}

/*
 * The target arithmetic.  Point ARCH_HOST_VA_BASE at an arena standing in for
 * 0xE82604 and check that the biased address the instructions compute,
 * base + idx * 0x20 - 0x20, is the entry VOLX_$ENTRY(idx) names - and that
 * the unbiased form the macros used to have runs off the end.
 */
TEST(biased_target_addresses_resolve_to_the_same_entries)
{
    static uint8_t arena[VOLX_TABLE_BYTES];
    int16_t idx;

    ARCH_HOST_VA_BASE = (uintptr_t)arena - (uintptr_t)VOLX_$TABLE_ADDR;

    for (idx = 1; idx <= VOLX_MAX_VOLUMES; idx++) {
        /* `lsl.w #0x5,Dn` + `lea (0x0,A5,Dn*0x1),A0`, fields at -0x20..-0x02 */
        uint32_t biased = (uint32_t)(VOLX_$TABLE_ADDR + (uint32_t)idx * 0x20);
        uint8_t *entry = (uint8_t *)ARCH_VA_TO_PTR(biased) - 0x20;

        ASSERT_EQ((uintptr_t)&arena[(idx - 1) * 0x20], (uintptr_t)entry);
        ASSERT_EQ((uintptr_t)arena
                  + ((uintptr_t)VOLX_$ENTRY(idx) - (uintptr_t)VOLX_$TABLE),
                  (uintptr_t)entry);

        /* every field of entry idx lies inside the 0xC0-byte segment */
        ASSERT_EQ(1, entry >= arena && entry + 0x20 <= arena + sizeof(arena));

        /* the unbiased form the macros used to use is off by one entry */
        ASSERT_EQ((uintptr_t)entry + 0x20,
                  (uintptr_t)ARCH_VA_TO_PTR(biased));
    }

    /* index 6 unbiased would start at the first byte of the DISK_ segment */
    ASSERT_EQ(0xE826C4u,
              (uint32_t)(VOLX_$TABLE_ADDR + (uint32_t)VOLX_MAX_VOLUMES * 0x20));

    ARCH_HOST_VA_BASE = 0;
}

/* Field displacements off the biased pointer -> struct offsets. */
TEST(field_displacements_match_the_struct)
{
    ASSERT_EQ(0x20 - 0x20, __builtin_offsetof(volx_$entry_t, dir_uid));
    ASSERT_EQ(0x20 - 0x18, __builtin_offsetof(volx_$entry_t, lv_uid));
    ASSERT_EQ(0x20 - 0x10, __builtin_offsetof(volx_$entry_t, parent_uid));
    ASSERT_EQ(0x20 - 0x08, __builtin_offsetof(volx_$entry_t, dev));
    ASSERT_EQ(0x20 - 0x06, __builtin_offsetof(volx_$entry_t, bus));
    ASSERT_EQ(0x20 - 0x04, __builtin_offsetof(volx_$entry_t, ctlr));
    ASSERT_EQ(0x20 - 0x02, __builtin_offsetof(volx_$entry_t, lv_num));
}

/* FIND_VOLX scans indices 1..6 - both ends inclusive - and no further. */
TEST(find_volx_scans_one_through_six)
{
    int16_t idx;

    clear_table();
    for (idx = 1; idx <= VOLX_MAX_VOLUMES; idx++) {
        fill_entry(idx);
    }

    for (idx = 1; idx <= VOLX_MAX_VOLUMES; idx++) {
        volx_$entry_t *e = raw_entry(idx);
        ASSERT_EQ(idx, FIND_VOLX(e->dev, e->bus, e->ctlr, e->lv_num));
    }

    /* a location held by no entry */
    ASSERT_EQ(0, FIND_VOLX(0x7000, 0x7001, 0x7002, 0x7003));
}

/* A zeroed table matches (0,0,0,0) at index 1, never at index 0. */
TEST(find_volx_never_returns_zero_for_a_match)
{
    clear_table();
    ASSERT_EQ(1, FIND_VOLX(0, 0, 0, 0));
}

/* VOLX_$REC_ENTRY writes entry idx and leaves its neighbours alone. */
TEST(rec_entry_writes_the_one_based_entry)
{
    int16_t idx;
    uid_t new_uid;

    for (idx = 1; idx <= VOLX_MAX_VOLUMES; idx++) {
        clear_table();

        new_uid.high = 0xABCD0000u | (uint32_t)idx;
        new_uid.low  = 0x0000EF00u | (uint32_t)idx;
        VOLX_$REC_ENTRY(&idx, &new_uid);

        ASSERT_EQ(new_uid.high, raw_entry(idx)->dir_uid.high);
        ASSERT_EQ(new_uid.low, raw_entry(idx)->dir_uid.low);

        /* nothing outside entry idx moved */
        {
            int16_t other;
            for (other = 1; other <= VOLX_MAX_VOLUMES; other++) {
                if (other == idx) {
                    continue;
                }
                ASSERT_EQ(0, raw_entry(other)->dir_uid.high);
                ASSERT_EQ(0, raw_entry(other)->dir_uid.low);
            }
        }
    }
}

/* GET_UIDS returns the lv_uid and dir_uid of the entry FIND_VOLX located. */
TEST(get_uids_reads_the_located_entry)
{
    int16_t idx;

    clear_table();
    for (idx = 1; idx <= VOLX_MAX_VOLUMES; idx++) {
        fill_entry(idx);
    }

    for (idx = 1; idx <= VOLX_MAX_VOLUMES; idx++) {
        volx_$entry_t *e = raw_entry(idx);
        int16_t dev = e->dev, bus = e->bus, ctlr = e->ctlr, lv = e->lv_num;
        uid_t lv_uid, dir_uid;
        status_$t status = 0x5A5A5A5Au;

        VOLX_$GET_UIDS(&dev, &bus, &ctlr, &lv, &lv_uid, &dir_uid, &status);

        ASSERT_EQ(status_$ok, status);
        ASSERT_EQ(e->lv_uid.high, lv_uid.high);
        ASSERT_EQ(e->lv_uid.low, lv_uid.low);
        ASSERT_EQ(e->dir_uid.high, dir_uid.high);
        ASSERT_EQ(e->dir_uid.low, dir_uid.low);
    }
}

/* A location held by no entry reports "logical volume not mounted". */
TEST(get_uids_reports_not_mounted)
{
    int16_t dev = 0x7000, bus = 0x7001, ctlr = 0x7002, lv = 0x7003;
    uid_t lv_uid = { 0x11111111u, 0x22222222u };
    uid_t dir_uid = { 0x33333333u, 0x44444444u };
    status_$t status = 0;

    clear_table();
    VOLX_$GET_UIDS(&dev, &bus, &ctlr, &lv, &lv_uid, &dir_uid, &status);

    ASSERT_EQ(status_$volume_logical_vol_not_mounted, status);
    /* the outputs are untouched on the miss path */
    ASSERT_EQ(0x11111111u, lv_uid.high);
    ASSERT_EQ(0x33333333u, dir_uid.high);
}

/* GET_INFO passes the index through to BAT and reads the same entry. */
TEST(get_info_reads_the_one_based_entry)
{
    int16_t idx;

    clear_table();
    for (idx = 1; idx <= VOLX_MAX_VOLUMES; idx++) {
        fill_entry(idx);
    }

    bat_n_free_status = status_$ok;
    for (idx = 1; idx <= VOLX_MAX_VOLUMES; idx++) {
        uid_t dir_uid = { 0, 0 };
        uint32_t freeb = 0, totalb = 0;
        status_$t status = 0;

        VOLX_$GET_INFO(&idx, &dir_uid, &freeb, &totalb, &status);

        ASSERT_EQ(status_$ok, status);
        ASSERT_EQ(idx, bat_n_free_idx);
        ASSERT_EQ(0x11112222u, freeb);
        ASSERT_EQ(0x33334444u, totalb);
        ASSERT_EQ(raw_entry(idx)->dir_uid.high, dir_uid.high);
        ASSERT_EQ(raw_entry(idx)->dir_uid.low, dir_uid.low);
    }
}

/* bat_$not_mounted is rewritten, and the entry is not read (0x00E6B614). */
TEST(get_info_translates_bat_not_mounted)
{
    int16_t idx = 3;
    uid_t dir_uid = { 0x9999u, 0x8888u };
    uint32_t freeb = 0, totalb = 0;
    status_$t status = 0;

    clear_table();
    fill_entry(idx);

    bat_n_free_status = bat_$not_mounted;
    VOLX_$GET_INFO(&idx, &dir_uid, &freeb, &totalb, &status);

    ASSERT_EQ(status_$volume_logical_vol_not_mounted, status);
    ASSERT_EQ(0x9999u, dir_uid.high);
    ASSERT_EQ(0x8888u, dir_uid.low);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    printf("VOLX entry addressing tests\n");
    printf("===========================\n");

    RUN_TEST(table_extent_is_the_map_segment);
    RUN_TEST(biased_target_addresses_resolve_to_the_same_entries);
    RUN_TEST(field_displacements_match_the_struct);
    RUN_TEST(find_volx_scans_one_through_six);
    RUN_TEST(find_volx_never_returns_zero_for_a_match);
    RUN_TEST(rec_entry_writes_the_one_based_entry);
    RUN_TEST(get_uids_reads_the_located_entry);
    RUN_TEST(get_uids_reports_not_mounted);
    RUN_TEST(get_info_reads_the_one_based_entry);
    RUN_TEST(get_info_translates_bat_not_mounted);

    printf("\n%d passed, %d failed\n", tests_passed - tests_failed,
           tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
