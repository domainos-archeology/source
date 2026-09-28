/*
 * name/test/test_map_dir.c - unit tests for name_$map_dir (0x00E58488)
 *
 * The real name/map_dir.c is #included; AST_$GET_LOCATION, MST_$MAPS and
 * CRASH_SYSTEM are mocked.  The tests pin:
 *   - the location gate (bad status, or bit 31 of location_info)  0x00E584D8
 *   - the MST_$MAPS argument list                                  0x00E584EA
 *   - first_base receiving the A0 result even on failure           0x00E58514
 *   - the LOW-word status test                                     0x00E5851C
 *   - the 0x10000 length check and Naming_Internal_Err crash       0x00E58522
 *   - the filled-in record and the 0xFF result                     0x00E58534
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-52s ", #name);                  \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "name/name_internal.h"

status_$t Naming_Internal_Err = 0x000E0025;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int        loc_calls;
static status_$t  loc_status;
static uint32_t   loc_info;
static uid_t      loc_uid_seen;
static uint16_t   loc_flags_seen;
static int8_t     loc_rec_flags_seen;

void AST_$GET_LOCATION(file_$obj_loc_t *loc_rec, uint16_t flags,
                       uint32_t *unused, uint32_t *location_out,
                       status_$t *status)
{
    loc_calls++;
    loc_uid_seen = loc_rec->uid;
    loc_flags_seen = flags;
    loc_rec_flags_seen = loc_rec->flags;
    *unused = 0xDEADBEEF;
    *location_out = loc_info;
    *status = loc_status;
}

static int        maps_calls;
static void      *maps_result;
static uint32_t   maps_len_out;
static status_$t  maps_status;
static int16_t    maps_asid_seen;
static boolean    maps_dir_seen;
static uid_t      maps_uid_seen;
static uint32_t   maps_start_seen;
static uint32_t   maps_len_seen;
static int16_t    maps_area_seen;
static uint32_t   maps_size_seen;
static boolean    maps_rights_seen;

void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    maps_calls++;
    maps_asid_seen = asid;
    maps_dir_seen = direction;
    maps_uid_seen = *uid;
    maps_start_seen = start_va;
    maps_len_seen = length;
    maps_area_seen = area_id;
    maps_size_seen = area_size;
    maps_rights_seen = access_rights;
    *(uint32_t *)map_info = maps_len_out;
    *status = maps_status;
    return maps_result;
}

static int              crash_calls;
static const status_$t *crash_status_seen;

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status_seen = status_p;
}

#include "../map_dir.c"

static const uid_t DIR_UID = { 0x12345678, 0x9ABCDEF0 };
static char        fake_page[0x10000] __attribute__((aligned(0x10000)));

static void reset(void)
{
    loc_calls = 0;
    loc_status = status_$ok;
    loc_info = 0x00001234;
    maps_calls = 0;
    maps_result = fake_page;
    maps_len_out = 0x10000;
    maps_status = status_$ok;
    crash_calls = 0;
}

TEST(success_fills_record_and_returns_true)
{
    name_$mapped_info_t info;
    status_$t st = 0x55;
    uid_t uid = DIR_UID;
    boolean r;

    reset();
    memset(&info, 0xEE, sizeof(info));
    r = name_$map_dir(&uid, 7, &info, &st);

    ASSERT_EQ(0xFF, (uint8_t)r);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(0, loc_flags_seen);
    ASSERT_EQ(DIR_UID.high, loc_uid_seen.high);
    ASSERT_EQ(DIR_UID.low, loc_uid_seen.low);
    ASSERT_EQ(0, loc_rec_flags_seen & FILE_OBJ_LOC_SCRATCH);

    ASSERT_EQ(1, maps_calls);
    ASSERT_EQ(7, maps_asid_seen);
    ASSERT_EQ(0xFF, (uint8_t)maps_dir_seen);
    ASSERT_EQ(DIR_UID.high, maps_uid_seen.high);
    ASSERT_EQ(0, maps_start_seen);
    ASSERT_EQ(0x10000, maps_len_seen);
    ASSERT_EQ(0x16, maps_area_seen);
    ASSERT_EQ(0, maps_size_seen);
    ASSERT_EQ(0xFF, (uint8_t)maps_rights_seen);

    ASSERT_EQ(0xFF, (uint8_t)info.active);
    ASSERT_EQ(0, info.reserved_02);
    ASSERT_EQ(ARCH_PTR_TO_VA(fake_page), info.first_base);
    ASSERT_EQ(1, info.entry_count);
    ASSERT_EQ(ARCH_PTR_TO_VA(fake_page) + 0x8000, info.second_base);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, crash_calls);
}

/* 0x00E584D8: AST status is copied to the caller, nothing mapped */
TEST(location_error_returns_false_with_ast_status)
{
    name_$mapped_info_t info;
    status_$t st = 0x55;
    uid_t uid = DIR_UID;

    reset();
    loc_status = 0x00070001;
    memset(&info, 0xEE, sizeof(info));
    ASSERT_EQ(0, (uint8_t)name_$map_dir(&uid, 1, &info, &st));
    ASSERT_EQ(0x00070001, st);
    ASSERT_EQ(0, maps_calls);
    ASSERT_EQ(0, (uint8_t)info.active);         /* cleared at 0x00E584A4 */
    ASSERT_EQ(0xEEEEEEEE, info.first_base);     /* untouched */
}

/* 0x00E584DE `tst.w (-0x40,A6) / bpl`: bit 31 of location_info set */
TEST(negative_location_returns_false_with_ok_status)
{
    name_$mapped_info_t info;
    status_$t st = 0x55;
    uid_t uid = DIR_UID;

    reset();
    loc_info = 0x80000000;
    ASSERT_EQ(0, (uint8_t)name_$map_dir(&uid, 1, &info, &st));
    ASSERT_EQ(status_$ok, st);                  /* the AST status, which was ok */
    ASSERT_EQ(0, maps_calls);

    /* only the HIGH word matters: 0x00008000 is fine */
    reset();
    loc_info = 0x00008000;
    ASSERT_EQ(0xFF, (uint8_t)name_$map_dir(&uid, 1, &info, &st));
    ASSERT_EQ(1, maps_calls);
}

/* 0x00E58514 then 0x00E5851C: the A0 result is stored before the LOW word
 * of the status is tested. */
TEST(map_failure_low_word_leaves_address_returns_false)
{
    name_$mapped_info_t info;
    status_$t st = 0;
    uid_t uid = DIR_UID;

    reset();
    maps_status = 0x00010002;
    memset(&info, 0xEE, sizeof(info));
    ASSERT_EQ(0, (uint8_t)name_$map_dir(&uid, 1, &info, &st));
    ASSERT_EQ(0x00010002, st);
    ASSERT_EQ(ARCH_PTR_TO_VA(fake_page), info.first_base);
    ASSERT_EQ(0, (uint8_t)info.active);
    ASSERT_EQ(0xEEEE, info.entry_count);        /* not filled in */
    ASSERT_EQ(0, crash_calls);
}

/* a status with only its HIGH word set does not stop the routine */
TEST(map_status_high_word_only_is_ignored)
{
    name_$mapped_info_t info;
    status_$t st = 0;
    uid_t uid = DIR_UID;

    reset();
    maps_status = 0x00010000;
    ASSERT_EQ(0xFF, (uint8_t)name_$map_dir(&uid, 1, &info, &st));
    ASSERT_EQ(0x00010000, st);
    ASSERT_EQ(1, info.entry_count);
}

/* 0x00E58522-0x00E5852E: length other than 0x10000 -> CRASH_SYSTEM(&0xE0025) */
TEST(wrong_length_crashes_with_naming_internal_err)
{
    name_$mapped_info_t info;
    status_$t st = 0;
    uid_t uid = DIR_UID;

    reset();
    maps_len_out = 0x8000;
    ASSERT_EQ(0xFF, (uint8_t)name_$map_dir(&uid, 1, &info, &st));
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ((uintptr_t)&Naming_Internal_Err, (uintptr_t)crash_status_seen);
    ASSERT_EQ(0x000E0025, *crash_status_seen);
}

int main(void)
{
    printf("name_$map_dir tests\n");
    RUN_TEST(success_fills_record_and_returns_true);
    RUN_TEST(location_error_returns_false_with_ast_status);
    RUN_TEST(negative_location_returns_false_with_ok_status);
    RUN_TEST(map_failure_low_word_leaves_address_returns_false);
    RUN_TEST(map_status_high_word_only_is_ignored);
    RUN_TEST(wrong_length_crashes_with_naming_internal_err);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
