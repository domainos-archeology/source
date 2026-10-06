/*
 * dir/test/test_read_def_prot.c - dir_$read_def_prot (0x00E51C6A)
 *
 * Pins: page 0 is mapped; ACL_$DIR_ACL reads the 11 longwords at +0x1A and
 * the UID at +0x46, ACL_$FILE_ACL those at +0x4E / +0x7A; any other type is
 * status_$naming_bad_type with the outputs untouched.
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
static uint8_t page[0x400] __attribute__((aligned(4)));
static int16_t page_seen;

void *dir_$map_page(void *handle, int16_t page_idx)
{
    (void)handle;
    page_seen = page_idx;
    return page;
}

#include "../read_def_prot.c"

static void setup(void)
{
    int i;
    for (i = 0; i < 0x400; i++) page[i] = (uint8_t)i;
    page_seen = -1;
}

TEST(dir_slot)
{
    uint32_t prot[11]; uid_t acl; status_$t st = 5;
    dir_$def_prot_t *s = (dir_$def_prot_t *)(page + 0x1A);
    setup();
    dir_$read_def_prot(0x100, &ACL_$DIR_ACL, prot, &acl, &st);
    ASSERT_EQ(0, page_seen);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, memcmp(prot, page + 0x1A, 44));
    ASSERT_EQ(s->acl_uid.high, acl.high);
    ASSERT_EQ(0, memcmp(&acl, page + 0x46, 8));
}

TEST(file_slot)
{
    uint32_t prot[11]; uid_t acl; status_$t st = 5;
    setup();
    dir_$read_def_prot(0x100, &ACL_$FILE_ACL, prot, &acl, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, memcmp(prot, page + 0x4E, 44));
    ASSERT_EQ(0, memcmp(&acl, page + 0x7A, 8));
}

TEST(bad_type)
{
    uint32_t prot[11]; uid_t acl = { 7, 7 }; status_$t st = 0;
    uid_t other = { 1, 2 };
    setup();
    prot[0] = 0x55;
    dir_$read_def_prot(0x100, &other, prot, &acl, &st);
    ASSERT_EQ(status_$naming_bad_type, st);
    ASSERT_EQ(0x55, prot[0]);
    ASSERT_EQ(7, acl.high);
}

int main(void)
{
    printf("dir_$read_def_prot tests\n");
    RUN_TEST(dir_slot);
    RUN_TEST(file_slot);
    RUN_TEST(bad_type);
    TEST_SUMMARY();
}
