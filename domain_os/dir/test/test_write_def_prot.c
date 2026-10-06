/*
 * dir/test/test_write_def_prot.c - dir_$write_def_prot (0x00E51E18)
 *
 * Pins: a plain source copies the block and UID, a "funky" one goes through
 * ACL_$CONVERT_FUNKY_ACL and loses bit 24 of the UID low half; a non-nil
 * ACL must be local on the directory volume (else
 * file_$objects_on_different_volumes); ACL_$SET_DEF_ACL_CHECK FALSE stops
 * everything; the slot at +0x1A/+0x46 (dirs) or +0x4E/+0x7A (files) is
 * written; a changed UID raises attribute 6 on the new ACL and truncates
 * the old one; the page is flushed when flush_flag < 0 even when the UID
 * did not change; a bad type is status_$naming_bad_type.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed); \
    return tests_failed ? 1 : 0; \
} while (0)

#include "dir/dir_internal.h"

uid_t ACL_$DIR_ACL = { 0x0000DDDD, 0x00000001 };
uid_t ACL_$FILE_ACL = { 0x0000FFFF, 0x00000002 };
const uint16_t DIR_$CONST_FOUR_W = 4;
uint32_t DIR_$CONST_ZERO_L = 0;
const int32_t DIR_$ONE_PAGE_L = 0x400;

static uint8_t arena[0x800] __attribute__((aligned(8)));
#define HVA 0x100
#define H ((dir_$handle_t *)(arena))
static uint8_t page[0x400] __attribute__((aligned(4)));

static int8_t check_result;
static int ncheck, nconv, nloc, nattr, ntrunc, nflush;
static status_$t loc_status;
static int16_t loc_volume;
static int8_t loc_flags;
static uint16_t attr_id_seen;
static uid_t attr_uid_seen, trunc_uid_seen, check_uid_seen;
static const uint16_t *check_four_seen;
static uid_t *flush_uid_seen;
static uint32_t *flush_len_seen;

void *dir_$map_page(void *handle, int16_t page_idx)
{
    (void)handle; (void)page_idx;
    return page;
}
void ACL_$CONVERT_FUNKY_ACL(void *acl_uid, void *acl_data_out,
                             void *prot_info_out, void *target_uid_out,
                             status_$t *status_ret)
{
    uid_t *u = (uid_t *)prot_info_out;
    (void)acl_uid; (void)target_uid_out;
    nconv++;
    memset(acl_data_out, 0x77, 44);
    u->high = 0x44000000;
    u->low = 0xFFFFFFFF;
    *status_ret = 0;
}
void AST_$GET_LOCATION(file_$obj_loc_t *loc_rec, uint16_t flags,
                       uint32_t *unused, uint32_t *location_out,
                       status_$t *status)
{
    (void)flags; (void)unused; (void)location_out;
    nloc++;
    loc_rec->volume = (uint16_t)loc_volume;
    loc_rec->flags = loc_flags;
    *status = loc_status;
}
int8_t ACL_$SET_DEF_ACL_CHECK(uid_t *dir_uid, void *prot_data, uid_t *acl_uid,
                              const uint16_t *unused_4, uid_t *acl_type,
                              status_$t *status_ret)
{
    (void)dir_uid; (void)prot_data; (void)acl_type;
    ncheck++;
    check_uid_seen = *acl_uid;
    check_four_seen = unused_4;
    *status_ret = 0;
    return check_result;
}
void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value,
                        status_$t *status)
{
    (void)value;
    nattr++;
    attr_id_seen = attr_id;
    attr_uid_seen = *uid;
    *status = 0;
}
void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   boolean *result, status_$t *status)
{
    (void)new_size; (void)flags; (void)result;
    ntrunc++;
    trunc_uid_seen = *uid;
    *status = 0;
}
void FILE_$FW_PARTIAL(uid_t *file_uid, uint32_t *start_offset,
                      uint32_t *byte_count, status_$t *status_ret)
{
    (void)start_offset;
    nflush++;
    flush_uid_seen = file_uid;
    flush_len_seen = byte_count;
    *status_ret = 0;
}

#include "../write_def_prot.c"

static uint32_t prot[11];

static void setup(void)
{
    int i;
    memset(arena, 0, sizeof(arena));
    memset(page, 0, sizeof(page));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - HVA;
    H->volume = 3;
    for (i = 0; i < 11; i++) prot[i] = 0x1000 + i;
    check_result = (int8_t)0xFF;
    ncheck = nconv = nloc = nattr = ntrunc = nflush = 0;
    loc_status = 0; loc_volume = 3; loc_flags = 0;
}

TEST(dir_slot_new_acl)
{
    uid_t src = { 0x12000001, 0x00000002 };
    status_$t st = 9;
    setup();
    *(uint32_t *)(page + 0x46) = 0x34000000;    /* old ACL, non-nil */
    dir_$write_def_prot(HVA, &ACL_$DIR_ACL, prot, &src, (int8_t)0xFF, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, nconv);
    ASSERT_EQ(1, nloc);
    ASSERT_EQ(1, ncheck);
    ASSERT_EQ(1, check_four_seen == &DIR_$CONST_FOUR_W);
    ASSERT_EQ(0, memcmp(page + 0x1A, prot, 44));
    ASSERT_EQ(0x12000001, *(uint32_t *)(page + 0x46));
    ASSERT_EQ(1, nattr);
    ASSERT_EQ(6, attr_id_seen);
    ASSERT_EQ(1, ntrunc);
    ASSERT_EQ(0x34000000, trunc_uid_seen.high);
    ASSERT_EQ(1, nflush);
    ASSERT_EQ(1, flush_uid_seen == &H->uid);
    ASSERT_EQ(0x400, *flush_len_seen);
}

TEST(same_uid_still_flushes)
{
    uid_t src = { 0x00000005, 0x00000006 };
    status_$t st = 9;
    setup();
    *(uint32_t *)(page + 0x7A) = 0x00000005;
    *(uint32_t *)(page + 0x7E) = 0x00000006;
    dir_$write_def_prot(HVA, &ACL_$FILE_ACL, prot, &src, (int8_t)0xFF, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, nloc);                     /* first byte 0: no volume check */
    ASSERT_EQ(0, memcmp(page + 0x4E, prot, 44));
    ASSERT_EQ(0, nattr);
    ASSERT_EQ(0, ntrunc);
    ASSERT_EQ(1, nflush);
}

TEST(no_flush_when_flag_clear)
{
    uid_t src = { 0x00000005, 0x00000007 };
    status_$t st = 9;
    setup();
    dir_$write_def_prot(HVA, &ACL_$FILE_ACL, prot, &src, 0, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, nflush);
    ASSERT_EQ(0, nattr);                    /* new first byte 0 */
    ASSERT_EQ(0, ntrunc);                   /* old first byte 0 */
}

TEST(other_volume)
{
    uid_t src = { 0x12000001, 0x00000002 };
    status_$t st = 9;
    setup();
    loc_volume = 4;
    dir_$write_def_prot(HVA, &ACL_$DIR_ACL, prot, &src, (int8_t)0xFF, &st);
    ASSERT_EQ(file_$objects_on_different_volumes, st);
    ASSERT_EQ(0, ncheck);
}

TEST(remote_acl)
{
    uid_t src = { 0x12000001, 0x00000002 };
    status_$t st = 9;
    setup();
    loc_flags = (int8_t)0x80;
    dir_$write_def_prot(HVA, &ACL_$DIR_ACL, prot, &src, (int8_t)0xFF, &st);
    ASSERT_EQ(file_$objects_on_different_volumes, st);
}

TEST(location_error_passes)
{
    uid_t src = { 0x12000001, 0x00000002 };
    status_$t st = 9;
    setup();
    loc_status = 0x00123456;
    dir_$write_def_prot(HVA, &ACL_$DIR_ACL, prot, &src, (int8_t)0xFF, &st);
    ASSERT_EQ(0x00123456, st);
}

TEST(check_false_stops)
{
    uid_t src = { 0x00000005, 0x00000006 };
    status_$t st = 9;
    setup();
    check_result = 0;
    dir_$write_def_prot(HVA, &ACL_$DIR_ACL, prot, &src, (int8_t)0xFF, &st);
    ASSERT_EQ(0, page[0x1A]);
    ASSERT_EQ(0, nflush);
}

TEST(funky_source)
{
    /* the low half's high word 0x0800: (0x0800 & 0xFF0) >> 4 & 0xE0 = 0x80 */
    uid_t src = { 0x00000001, 0x08000000 };
    status_$t st = 9;
    setup();
    dir_$write_def_prot(HVA, &ACL_$DIR_ACL, prot, &src, 0, &st);
    ASSERT_EQ(1, nconv);
    ASSERT_EQ(0x44000000, check_uid_seen.high);
    ASSERT_EQ(0xFEFFFFFF, check_uid_seen.low);
    ASSERT_EQ(0x77, page[0x1A]);
}

TEST(bad_type)
{
    uid_t src = { 0x00000005, 0x00000006 };
    uid_t other = { 1, 2 };
    status_$t st = 9;
    setup();
    dir_$write_def_prot(HVA, &other, prot, &src, (int8_t)0xFF, &st);
    ASSERT_EQ(status_$naming_bad_type, st);
    ASSERT_EQ(0, nflush);
}

int main(void)
{
    printf("dir_$write_def_prot tests\n");
    RUN_TEST(dir_slot_new_acl);
    RUN_TEST(same_uid_still_flushes);
    RUN_TEST(no_flush_when_flag_clear);
    RUN_TEST(other_volume);
    RUN_TEST(remote_acl);
    RUN_TEST(location_error_passes);
    RUN_TEST(check_false_stops);
    RUN_TEST(funky_source);
    RUN_TEST(bad_type);
    TEST_SUMMARY();
}
