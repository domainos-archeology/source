/*
 * dir/test/test_get_parent_uid.c - dir_$get_parent_uid (0x00E4D060)
 *
 * Pins: AST_$GET_COMMON_ATTRIBUTES is asked (selector 0x80) about a
 * location record holding the UID with flags bit 6 cleared; sub-type 1 or
 * 2 replaces the UID by the record UID at +8; another sub-type is
 * branch_is_not_a_directory; file_$object_not_found, or any error with the
 * remote bit, becomes directory_object_not_found; other errors pass.
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

static status_$t ast_status;
static uint8_t ast_sub_type;
static int8_t ast_flags;
static uint16_t sel_seen;
static uid_t uid_seen;
static int8_t flags_in_seen;

void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    sel_seen = flags;
    uid_seen = loc_rec->uid;
    flags_in_seen = loc_rec->flags;
    memset(attrs, 0, sizeof(*attrs));
    attrs->sub_type = ast_sub_type;
    attrs->dirptr.high = 0xAAAA0001;
    attrs->dirptr.low = 0xBBBB0002;
    loc_rec->flags = ast_flags;
    *status = ast_status;
}

#include "../get_parent_uid.c"

static void run(uid_t *u, status_$t *st, status_$t s, uint8_t sub, int8_t f)
{
    ast_status = s;
    ast_sub_type = sub;
    ast_flags = f;
    u->high = 0x11;
    u->low = 0x22;
    dir_$get_parent_uid(u, st);
}

TEST(directory_gives_parent)
{
    uid_t u; status_$t st;
    run(&u, &st, 0, 2, 0);
    ASSERT_EQ(0x80, sel_seen);
    ASSERT_EQ(0x11, uid_seen.high);
    ASSERT_EQ(0x22, uid_seen.low);
    ASSERT_EQ(0, flags_in_seen & 0x40);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0xAAAA0001, u.high);
    ASSERT_EQ(0xBBBB0002, u.low);
}

TEST(not_a_directory)
{
    uid_t u; status_$t st;
    run(&u, &st, 0, 0, 0);
    ASSERT_EQ(status_$naming_branch_is_not_a_directory, st);
    ASSERT_EQ(0x11, u.high);
}

TEST(object_not_found_maps)
{
    uid_t u; status_$t st;
    run(&u, &st, file_$object_not_found, 0, 0);
    ASSERT_EQ(status_$naming_directory_object_not_found, st);
}

TEST(remote_error_maps)
{
    uid_t u; status_$t st;
    run(&u, &st, 0x00123456, 0, (int8_t)0x80);
    ASSERT_EQ(status_$naming_directory_object_not_found, st);
}

TEST(local_error_passes)
{
    uid_t u; status_$t st;
    run(&u, &st, 0x00123456, 0, 0);
    ASSERT_EQ(0x00123456, st);
}

int main(void)
{
    printf("dir_$get_parent_uid tests\n");
    RUN_TEST(directory_gives_parent);
    RUN_TEST(not_a_directory);
    RUN_TEST(object_not_found_maps);
    RUN_TEST(remote_error_maps);
    RUN_TEST(local_error_passes);
    TEST_SUMMARY();
}
