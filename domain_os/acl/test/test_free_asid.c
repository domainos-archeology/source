/*
 * acl/test/test_free_asid.c - unit tests for ACL_$FREE_ASID (0x00E74C6A).
 *
 * acl/free_asid.c is #included below.  The point of the file is bead
 * source-6vuq: the `lea (0xb8,PC),A3` at 0x00E74CE2 names the 12-byte cell at
 * 0x00E74D9C, whose image bytes are 0000000d 0000000d 0000000d - not zeros.
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

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

acl_sid_block_t ACL_$CURRENT_SIDS[PROC1_MAX_PROCESSES];
acl_sid_block_t ACL_$ORIGINAL_SIDS[PROC1_MAX_PROCESSES];
acl_sid_block_t ACL_$SAVED_SIDS[PROC1_MAX_PROCESSES];
acl_proj_list_t ACL_$PROJ_LISTS[PROC1_MAX_PROCESSES];
acl_proj_list_t ACL_$SAVED_PROJ[PROC1_MAX_PROCESSES];
uid_t           ACL_$PROJ_UIDS[PROC1_MAX_PROCESSES][ACL_MAX_PROJECTS];
int16_t         ACL_$SUBSYS_LEVEL[PROC1_MAX_PROCESSES];
uint8_t         ACL_$ASID_FREE_BITMAP[8];
uint8_t         ACL_$ASID_SUSER_BITMAP[8];

uid_t UID_$NIL             = { 0, 0 };
uid_t RGYC_$P_SYS_USER_UID = { 0x0000041Cu, 0x00000001u };  /* 0x00E1741C */
uid_t RGYC_$G_SYS_PROJ_UID = { 0x00000424u, 0x00000002u };  /* 0x00E17424 */
uid_t RGYC_$O_SYS_ORG_UID  = { 0x0000043Cu, 0x00000003u };  /* 0x00E1743C */

#include "../free_asid.c"

#define TEST_ASID 5

static status_$t run_free(int16_t asid)
{
    status_$t status = 0x11111111;

    memset(ACL_$CURRENT_SIDS, 0xEE, sizeof(ACL_$CURRENT_SIDS));
    memset(ACL_$ORIGINAL_SIDS, 0xEE, sizeof(ACL_$ORIGINAL_SIDS));
    memset(ACL_$SAVED_SIDS, 0xEE, sizeof(ACL_$SAVED_SIDS));
    memset(ACL_$PROJ_LISTS, 0xEE, sizeof(ACL_$PROJ_LISTS));
    memset(ACL_$SAVED_PROJ, 0xEE, sizeof(ACL_$SAVED_PROJ));
    memset(ACL_$PROJ_UIDS, 0xEE, sizeof(ACL_$PROJ_UIDS));
    memset(ACL_$SUBSYS_LEVEL, 0xEE, sizeof(ACL_$SUBSYS_LEVEL));
    memset(ACL_$ASID_FREE_BITMAP, 0x00, sizeof(ACL_$ASID_FREE_BITMAP));
    memset(ACL_$ASID_SUSER_BITMAP, 0xFF, sizeof(ACL_$ASID_SUSER_BITMAP));

    ACL_$FREE_ASID(asid, &status);
    return status;
}

/*
 * The default project-list cell at 0x00E74D9C:
 *   00 00 00 0d  00 00 00 0d  00 00 00 0d
 */
TEST(default_proj_list_constant)
{
    ASSERT_EQ(0x0000000Du, DEFAULT_PROJ_LIST.field_00);
    ASSERT_EQ(0x0000000Du, DEFAULT_PROJ_LIST.field_04);
    ASSERT_EQ(0x0000000Du, DEFAULT_PROJ_LIST.field_08);
    ASSERT_EQ(12, sizeof(acl_proj_list_t));
}

/* 0x00E74CE0-0x00E74D1F: the constant reaches both project-list cells. */
TEST(proj_lists_get_the_constant)
{
    run_free(TEST_ASID);

    ASSERT_EQ(0x0000000Du, ACL_$PROJ_LISTS[TEST_ASID].field_00);
    ASSERT_EQ(0x0000000Du, ACL_$PROJ_LISTS[TEST_ASID].field_04);
    ASSERT_EQ(0x0000000Du, ACL_$PROJ_LISTS[TEST_ASID].field_08);

    /* 0x00E74D12: SAVED_PROJ is copied FROM the just-written PROJ_LISTS. */
    ASSERT_EQ(0x0000000Du, ACL_$SAVED_PROJ[TEST_ASID].field_00);
    ASSERT_EQ(0x0000000Du, ACL_$SAVED_PROJ[TEST_ASID].field_08);

    /* Neighbouring rows untouched. */
    ASSERT_EQ(0xEEEEEEEEu, ACL_$PROJ_LISTS[TEST_ASID - 1].field_00);
    ASSERT_EQ(0xEEEEEEEEu, ACL_$PROJ_LISTS[TEST_ASID + 1].field_00);
}

/* 0x00E74CAE-0x00E74CDF then 0x00E74CF0-0x00E74D11. */
TEST(sid_defaults_and_copies)
{
    run_free(TEST_ASID);

    ASSERT_EQ(0x0000041Cu, ACL_$CURRENT_SIDS[TEST_ASID].user_sid.high);
    ASSERT_EQ(0x00000424u, ACL_$CURRENT_SIDS[TEST_ASID].group_sid.high);
    ASSERT_EQ(0x0000043Cu, ACL_$CURRENT_SIDS[TEST_ASID].org_sid.high);
    ASSERT_EQ(0u,          ACL_$CURRENT_SIDS[TEST_ASID].login_sid.high);

    /* The whole 36-byte block, `pad` included, is copied twice. */
    ASSERT_EQ(0, memcmp(&ACL_$ORIGINAL_SIDS[TEST_ASID],
                        &ACL_$CURRENT_SIDS[TEST_ASID], 36));
    ASSERT_EQ(0, memcmp(&ACL_$SAVED_SIDS[TEST_ASID],
                        &ACL_$CURRENT_SIDS[TEST_ASID], 36));
}

/* 0x00E74D20-0x00E74D5D */
TEST(clears_proj_uids_and_subsys_level)
{
    int i;

    run_free(TEST_ASID);

    for (i = 0; i < 8; i++) {
        ASSERT_EQ(0u, ACL_$PROJ_UIDS[TEST_ASID][i].high);
        ASSERT_EQ(0u, ACL_$PROJ_UIDS[TEST_ASID][i].low);
    }
    ASSERT_EQ(0xEEEEEEEEu, ACL_$PROJ_UIDS[TEST_ASID + 1][0].high);
    ASSERT_EQ(0, ACL_$SUBSYS_LEVEL[TEST_ASID]);
}

/* 0x00E74D5E-0x00E74D8B: byte (asid-1)>>3, mask 0x80 >> ((asid-1)&7). */
TEST(bitmaps)
{
    status_$t status;

    status = run_free(5);
    ASSERT_EQ(0x08, ACL_$ASID_FREE_BITMAP[0]);   /* 0x80 >> ((5-1) & 7) */
    ASSERT_EQ(0xF7, ACL_$ASID_SUSER_BITMAP[0]);
    ASSERT_EQ(status_$ok, status);

    run_free(64);
    ASSERT_EQ(0x01, ACL_$ASID_FREE_BITMAP[7]);
    ASSERT_EQ(0xFE, ACL_$ASID_SUSER_BITMAP[7]);
    ASSERT_EQ(0x00, ACL_$ASID_FREE_BITMAP[0]);

    run_free(1);
    ASSERT_EQ(0x80, ACL_$ASID_FREE_BITMAP[0]);
    ASSERT_EQ(0x7F, ACL_$ASID_SUSER_BITMAP[0]);
}

int main(void)
{
    printf("ACL_$FREE_ASID tests\n");

    RUN_TEST(default_proj_list_constant);
    RUN_TEST(proj_lists_get_the_constant);
    RUN_TEST(sid_defaults_and_copies);
    RUN_TEST(clears_proj_uids_and_subsys_level);
    RUN_TEST(bitmaps);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
