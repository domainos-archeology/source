/*
 * name/test/test_old_get_root_entry.c - name_$old_get_root_entry
 * (0x00E57F74)
 *
 * Pins: the local lookup runs first; only name_not_found in NAME_$ROOT_UID
 * asks REM_NAME_$GET_ENTRY (with the address of name_len); a type-1 answer
 * fills {1, UID, extra} and is cached with name_$old_add_entry(type 0,
 * the unmapped name, the UID, extra); anything else is name_not_found.
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

#include "name/name_internal.h"
#include "dir/dir.h"
#include "rem_name/rem_name.h"

name_$data_t NAME_$DATA;
const int16_t name_$leaf_max_len_00e544ae = 0x0020;

static status_$t local_status, rem_status;
static uint16_t rem_type;
static int nlocal, nrem, nadd;
static uint16_t *rem_len_ptr_seen;
static uint16_t add_type_seen, add_len_seen;
static uint32_t add_extra_seen;
static uid_t add_uid_seen;
static char add_name_seen[0x20];
static int16_t unmap_max_seen;

void name_$old_get_entry_nonroot(uid_t *dir_uid, char *name, uint16_t name_len,
                                 dir_$old_entry_t *entry_ret,
                                 status_$t *status_ret)
{
    (void)dir_uid; (void)name; (void)name_len; (void)entry_ret;
    nlocal++;
    *status_ret = local_status;
}
void REM_NAME_$GET_ENTRY(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret)
{
    dir_$rep_entry_t *r = (dir_$rep_entry_t *)entry_ret;
    (void)dir_uid; (void)name;
    nrem++;
    rem_len_ptr_seen = name_len;
    memset(r, 0, sizeof(*r));
    r->hdr = rem_type;
    r->name_len = 3;
    memcpy(r->name, "ABC", 3);
    r->uid.high = 0x61;
    r->uid.low = 0x62;
    r->extra = 0x63;
    *status_ret = rem_status;
}
void UNMAP_CASE(char *name, int16_t *name_len, char *output,
                int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    int i;
    unmap_max_seen = *max_out_len;
    for (i = 0; i < *name_len; i++) output[i] = (char)(name[i] | 0x20);
    *out_len = *name_len;
    *truncated = 0;
}
void name_$old_add_entry(uid_t *dir_uid, uint16_t type, char *name,
                         uint16_t name_len, uid_t *file_uid,
                         uint32_t flags, status_$t *status_ret)
{
    (void)dir_uid;
    nadd++;
    add_type_seen = type;
    add_len_seen = name_len;
    memcpy(add_name_seen, name, name_len);
    add_uid_seen = *file_uid;
    add_extra_seen = flags;
    *status_ret = 0x99;                 /* private: not reported */
}

#include "../old_get_root_entry.c"

static dir_$old_entry_t e;

static void setup(void)
{
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    NAME_$DATA.root_uid.high = 0x1234;
    NAME_$DATA.root_uid.low = 0x5678;
    local_status = status_$naming_name_not_found;
    rem_status = 0;
    rem_type = 1;
    nlocal = nrem = nadd = 0;
    memset(&e, 0, sizeof(e));
}

TEST(found_locally)
{
    status_$t st = 9;
    setup();
    local_status = 0;
    name_$old_get_root_entry(&NAME_$DATA.root_uid, "abc", 3, &e, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, nrem);
}

TEST(not_root_no_remote)
{
    status_$t st = 9;
    uid_t other = { 1, 2 };
    setup();
    name_$old_get_root_entry(&other, "abc", 3, &e, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
    ASSERT_EQ(0, nrem);
}

TEST(replicated_root_caches)
{
    status_$t st = 9;
    setup();
    name_$old_get_root_entry(&NAME_$DATA.root_uid, "abc", 3, &e, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, nrem);
    ASSERT_EQ(3, *rem_len_ptr_seen);
    ASSERT_EQ(1, e.type);
    ASSERT_EQ(0x61, e.uid.high);
    ASSERT_EQ(0x62, e.uid.low);
    ASSERT_EQ(0x63, e.extra);
    ASSERT_EQ(0x20, unmap_max_seen);
    ASSERT_EQ(1, nadd);
    ASSERT_EQ(0, add_type_seen);
    ASSERT_EQ(3, add_len_seen);
    ASSERT_EQ(0, memcmp(add_name_seen, "abc", 3));
    ASSERT_EQ(0x61, add_uid_seen.high);
    ASSERT_EQ(0x63, add_extra_seen);
}

TEST(remote_wrong_type)
{
    status_$t st = 9;
    setup();
    rem_type = 2;
    name_$old_get_root_entry(&NAME_$DATA.root_uid, "abc", 3, &e, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
    ASSERT_EQ(0, nadd);
}

TEST(remote_error)
{
    status_$t st = 9;
    setup();
    rem_status = 0x00110001;
    name_$old_get_root_entry(&NAME_$DATA.root_uid, "abc", 3, &e, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
}

int main(void)
{
    printf("name_$old_get_root_entry tests\n");
    RUN_TEST(found_locally);
    RUN_TEST(not_root_no_remote);
    RUN_TEST(replicated_root_caches);
    RUN_TEST(remote_wrong_type);
    RUN_TEST(remote_error);
    TEST_SUMMARY();
}
