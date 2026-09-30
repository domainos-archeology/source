/*
 * acl/test/test_alloc_asid.c - unit tests for ACL_$ALLOC_ASID (0x00E73BB8).
 */
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
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

#include "acl/acl_internal.h"

MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);

uid_t    UID_$NIL = { 0, 0 };
uint16_t PROC1_$CURRENT;

#include "../alloc_asid.c"

#define CUR  3
#define NEW  9

static void fill(void)
{
    int i;

    memset(&ACL_$DATA, 0xEE, sizeof(ACL_$DATA));
    memset(&ACL_$DATA.original_sids[CUR], 0x11, sizeof(acl_sid_block_t));
    memset(&ACL_$DATA.current_sids[CUR], 0x22, sizeof(acl_sid_block_t));
    memset(&ACL_$DATA.saved_sids[CUR], 0x33, sizeof(acl_sid_block_t));
    for (i = 0; i < 8; i++) {
        ACL_$DATA.proj_uids[CUR][i].high = 0x100u + i;
        ACL_$DATA.proj_uids[CUR][i].low = 0x200u + i;
    }
    memset(&ACL_$DATA.saved_proj[CUR], 0x44, sizeof(acl_proj_list_t));
    memset(&ACL_$DATA.proj_lists[CUR], 0x55, sizeof(acl_proj_list_t));
    PROC1_$CURRENT = CUR;
}

/* Current process's free bit SET: plain copy, login SIDs copied too. */
TEST(copy_when_current_marked_free)
{
    status_$t st = 0x12345678;
    int i;

    fill();
    ACL_$DATA.asid_free_bitmap[0] = 0x20;     /* pid 3: 0x80 >> 2 */
    ACL_$ALLOC_ASID(NEW, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, memcmp(&ACL_$DATA.current_sids[NEW], &ACL_$DATA.current_sids[CUR], 36));
    ASSERT_EQ(0x11111111u, ACL_$DATA.original_sids[NEW].login_sid.high);
    ASSERT_EQ(0x22222222u, ACL_$DATA.current_sids[NEW].login_sid.low);
    ASSERT_EQ(0x33333333u, ACL_$DATA.saved_sids[NEW].pad);
    for (i = 0; i < 8; i++) {
        ASSERT_EQ(0x100u + i, ACL_$DATA.proj_uids[NEW][i].high);
        ASSERT_EQ(0x200u + i, ACL_$DATA.proj_uids[NEW][i].low);
    }
    ASSERT_EQ(0x44444444u, ACL_$DATA.saved_proj[NEW].field_08);
    ASSERT_EQ(0x55555555u, ACL_$DATA.proj_lists[NEW].field_00);
    ASSERT_EQ(0xEEEEEEEEu, ACL_$DATA.proj_lists[NEW + 1].field_00);
}

/* 0x00E73CC0: bit clear: the three new login SIDs become UID_$NIL. */
TEST(nil_login_when_current_not_free)
{
    status_$t st = 0x12345678;

    fill();
    ACL_$DATA.asid_free_bitmap[0] = (uint8_t)~0x20u;
    /* The new asid's own bit is irrelevant: set it. */
    ACL_$DATA.asid_free_bitmap[1] = 0xFF;
    ACL_$ALLOC_ASID(NEW, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0u, ACL_$DATA.original_sids[NEW].login_sid.high);
    ASSERT_EQ(0u, ACL_$DATA.current_sids[NEW].login_sid.low);
    ASSERT_EQ(0u, ACL_$DATA.saved_sids[NEW].login_sid.high);
    ASSERT_EQ(0x22222222u, ACL_$DATA.current_sids[NEW].user_sid.high);
    /* the source rows keep theirs */
    ASSERT_EQ(0x22222222u, ACL_$DATA.current_sids[CUR].login_sid.low);
}

int main(void)
{
    printf("ACL_$ALLOC_ASID tests\n");
    RUN_TEST(copy_when_current_marked_free);
    RUN_TEST(nil_login_when_current_not_free);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
