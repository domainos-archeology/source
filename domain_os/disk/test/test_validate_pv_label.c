/*
 * disk/test/test_validate_pv_label.c - unit tests for disk_$validate_pv_label
 * (0x00E6C21A)
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

#include "disk/disk_internal.h"

uid_t PV_LABEL_$UID = { 0x00000207, 0 };

static disk_$pv_label_t label;
static status_$t get_status;
static int16_t got_vol;
static int32_t got_daddr;
static void *got_uid;

void *DISK_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *expected_uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    (void)block_hint; (void)block_type; (void)flags;
    got_vol = vol_idx; got_daddr = daddr; got_uid = expected_uid;
    *status = get_status;
    return &label;
}

#include "../validate_pv_label.c"

static status_$t st;

static void reset_state(void)
{
    memset(&label, 0, sizeof(label));
    memcpy(label.apollo, "APOLLO", 6);
    label.version = 1;
    label.num_parts = 1;
    get_status = 0;
    st = 0;
}

static void test_good_label(void)
{
    ASSERT_EQ((unsigned long)&label, (unsigned long)disk_$validate_pv_label(4, &st));
    ASSERT_EQ(0, st);
    ASSERT_EQ(4, got_vol);
    ASSERT_EQ(0, got_daddr);
    ASSERT_EQ((unsigned long)&PV_LABEL_$UID, (unsigned long)got_uid);
}

static void test_read_error_returns_nil(void)
{
    get_status = 0x00020005;
    ASSERT_EQ(0, (unsigned long)disk_$validate_pv_label(4, &st));
    ASSERT_EQ(0x00020005, st);
}

static void test_bad_signature_and_version(void)
{
    label.apollo[5] = 'X';
    ASSERT_EQ((unsigned long)&label, (unsigned long)disk_$validate_pv_label(1, &st));
    ASSERT_EQ(status_$invalid_physical_volume_label, st);
    reset_state();
    label.version = 2;
    disk_$validate_pv_label(1, &st);
    ASSERT_EQ(status_$invalid_physical_volume_label, st);
    reset_state();
    label.version = -1;                 /* unsigned compare */
    disk_$validate_pv_label(1, &st);
    ASSERT_EQ(status_$invalid_physical_volume_label, st);
}

static void test_member_counts(void)
{
    static const uint16_t good[] = { 0, 1, 2, 4, 8 };
    int i;
    for (i = 0; i < 5; i++) {
        reset_state();
        label.num_parts = good[i];
        disk_$validate_pv_label(1, &st);
        ASSERT_EQ(0, st);
    }
    reset_state();
    label.num_parts = 3;
    disk_$validate_pv_label(1, &st);
    ASSERT_EQ(status_$invalid_physical_volume_label, st);
}

static void test_interleave_for_striped_set(void)
{
    label.num_parts = 2;
    label.interleave = 3;               /* not in 0x37 */
    disk_$validate_pv_label(1, &st);
    ASSERT_EQ(status_$invalid_physical_volume_label, st);
    reset_state();
    label.num_parts = 2;
    label.interleave = 5;
    disk_$validate_pv_label(1, &st);
    ASSERT_EQ(0, st);
    reset_state();
    label.num_parts = 1;                /* single volume: not checked */
    label.interleave = 3;
    disk_$validate_pv_label(1, &st);
    ASSERT_EQ(0, st);
}

int main(void)
{
    printf("disk_$validate_pv_label tests\n");
    RUN_TEST(good_label);
    RUN_TEST(read_error_returns_nil);
    RUN_TEST(bad_signature_and_version);
    RUN_TEST(member_counts);
    RUN_TEST(interleave_for_striped_set);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
