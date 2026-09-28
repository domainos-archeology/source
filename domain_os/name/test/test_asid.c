/*
 * name/test/test_asid.c - unit tests for NAME_$INIT_ASID (0x00E73CFC),
 * NAME_$FORK (0x00E73E44) and NAME_$FREE_ASID (0x00E74DA8).
 *
 * The real name/asid.c is #included; ACL_$ENTER_SUPER / ACL_$EXIT_SUPER,
 * ACL_$RIGHTS, name_$map_dir and name_$unmap_dir_buffers are mocked so each
 * arm of the disassembly can be pinned.
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

uint16_t     PROC1_$AS_ID;
name_$data_t NAME_$DATA;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int super_depth;
static int super_max_depth;
static int enter_super_calls;
static int exit_super_calls;

void ACL_$ENTER_SUPER(void)
{
    enter_super_calls++;
    super_depth++;
    if (super_depth > super_max_depth) {
        super_max_depth = super_depth;
    }
}

void ACL_$EXIT_SUPER(void)
{
    exit_super_calls++;
    super_depth--;
}

/* ACL_$RIGHTS: results consumed in call order */
static uint32_t  rights_results[4];
static int       rights_calls;
static uid_t     rights_uid_seen[4];
static boolean   rights_ignore_super_seen[4];
static uint32_t  rights_mask_seen[4];
static int16_t   rights_opts_seen[4];
static int       rights_in_super_seen[4];

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status_ret)
{
    int n = rights_calls++;
    rights_uid_seen[n] = *uid;
    rights_ignore_super_seen[n] = *ignore_super;
    rights_mask_seen[n] = *required_mask;
    rights_opts_seen[n] = *option_flags;
    rights_in_super_seen[n] = super_depth;
    *status_ret = status_$ok;
    return rights_results[n];
}

/* name_$map_dir: status per call, records asid and which info slot */
static status_$t            map_status[4];
static int                  map_calls;
static uid_t                map_uid_seen[4];
static int16_t              map_asid_seen[4];
static name_$mapped_info_t *map_info_seen[4];

boolean name_$map_dir(uid_t *dir_uid, int16_t asid,
                      name_$mapped_info_t *mapped_info, status_$t *status_ret)
{
    int n = map_calls++;
    map_uid_seen[n] = *dir_uid;
    map_asid_seen[n] = asid;
    map_info_seen[n] = mapped_info;
    *status_ret = map_status[n];
    return (boolean)(map_status[n] == status_$ok ? -1 : 0);
}

static int                  unmap_calls;
static int16_t              unmap_asid_seen[4];
static name_$mapped_info_t *unmap_info_seen[4];

void name_$unmap_dir_buffers(int16_t asid, name_$mapped_info_t *mapped_info)
{
    int n = unmap_calls++;
    unmap_asid_seen[n] = asid;
    unmap_info_seen[n] = mapped_info;
}

#include "../asid.c"

/* ------------------------------------------------------------------ */

#define CUR_ASID   3
#define NEW_ASID   7

static const uid_t WDIR_UID = { 0x11111111, 0x22222222 };
static const uid_t NDIR_UID = { 0x33333333, 0x44444444 };
static const uid_t NODE_UID = { 0x55555555, 0x66666666 };
static const uid_t OLD_UID  = { 0xAAAAAAAA, 0xBBBBBBBB };

static void reset(void)
{
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    PROC1_$AS_ID = CUR_ASID;
    NAME_$DATA.wdir_uid[CUR_ASID] = WDIR_UID;
    NAME_$DATA.ndir_uid[CUR_ASID] = NDIR_UID;
    NAME_$DATA.wdir_uid[NEW_ASID] = OLD_UID;
    NAME_$DATA.ndir_uid[NEW_ASID] = OLD_UID;
    NAME_$DATA.node_uid = NODE_UID;

    super_depth = super_max_depth = enter_super_calls = exit_super_calls = 0;
    memset(rights_results, 0, sizeof(rights_results));
    rights_calls = 0;
    memset(map_status, 0, sizeof(map_status));
    map_calls = 0;
    unmap_calls = 0;
}

static int uid_eq(const uid_t *a, const uid_t *b)
{
    return a->high == b->high && a->low == b->low;
}

/* ------------------------------------------------------------------ */
/* NAME_$INIT_ASID                                                      */
/* ------------------------------------------------------------------ */

/* 0x00E73D30-0x00E73D42 / 0x00E73DBC-0x00E73DCE: the constant cells. */
TEST(init_passes_the_three_constant_cells)
{
    int16_t   asid = NEW_ASID;
    status_$t status = 0x12345678;

    reset();
    rights_results[0] = 1;
    rights_results[1] = 1;
    NAME_$INIT_ASID(&asid, &status);

    ASSERT_EQ(2, rights_calls);
    ASSERT_EQ(1, uid_eq(&rights_uid_seen[0], &WDIR_UID));
    ASSERT_EQ(1, uid_eq(&rights_uid_seen[1], &NDIR_UID));
    ASSERT_EQ(0xFF, (uint8_t)rights_ignore_super_seen[0]);
    ASSERT_EQ(0xFF, (uint8_t)rights_ignore_super_seen[1]);
    ASSERT_EQ(0xFFFFFFFFu, rights_mask_seen[0]);
    ASSERT_EQ(0xFFFFFFFFu, rights_mask_seen[1]);
    ASSERT_EQ(1, rights_opts_seen[0]);
    ASSERT_EQ(1, rights_opts_seen[1]);
    /* both queries run inside ENTER_SUPER / EXIT_SUPER (0x00E73D0C/0x00E73E2A) */
    ASSERT_EQ(1, rights_in_super_seen[0]);
    ASSERT_EQ(1, rights_in_super_seen[1]);
    ASSERT_EQ(1, enter_super_calls);
    ASSERT_EQ(1, exit_super_calls);
}

/* Both rights present, both maps succeed: UIDs stored, status ok. */
TEST(init_maps_and_stores_both_dirs)
{
    int16_t   asid = NEW_ASID;
    status_$t status = 0x12345678;

    reset();
    rights_results[0] = 0x0F;
    rights_results[1] = 0x01;
    NAME_$INIT_ASID(&asid, &status);

    ASSERT_EQ(2, map_calls);
    ASSERT_EQ(NEW_ASID, map_asid_seen[0]);
    ASSERT_EQ(NEW_ASID, map_asid_seen[1]);
    ASSERT_EQ((uintptr_t)&NAME_$DATA.wdir_mapped_info[NEW_ASID], (uintptr_t)map_info_seen[0]);
    ASSERT_EQ((uintptr_t)&NAME_$DATA.ndir_mapped_info[NEW_ASID], (uintptr_t)map_info_seen[1]);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[NEW_ASID], &WDIR_UID));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.ndir_uid[NEW_ASID], &NDIR_UID));
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E73D9C / 0x00E73E28: no rights -> status_$ok, nothing mapped/stored. */
TEST(init_no_rights_is_ok_and_leaves_uids)
{
    int16_t   asid = NEW_ASID;
    status_$t status = 0x12345678;

    reset();
    NAME_$INIT_ASID(&asid, &status);

    ASSERT_EQ(2, rights_calls);
    ASSERT_EQ(0, map_calls);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[NEW_ASID], &OLD_UID));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.ndir_uid[NEW_ASID], &OLD_UID));
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E73D7A bne.w 0x00E73E06: a wdir map failure sets bit 31 and skips
 * the ndir half entirely. */
TEST(init_wdir_map_failure_sets_bit31_and_skips_ndir)
{
    int16_t   asid = NEW_ASID;
    status_$t status = 0;

    reset();
    rights_results[0] = 1;
    rights_results[1] = 1;
    map_status[0] = 0x00050003;
    NAME_$INIT_ASID(&asid, &status);

    ASSERT_EQ(1, rights_calls);
    ASSERT_EQ(1, map_calls);
    ASSERT_EQ(0x80050003u, (uint32_t)status);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[NEW_ASID], &OLD_UID));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.ndir_uid[NEW_ASID], &OLD_UID));
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E73E02/0x00E73E06: an ndir map failure after a good wdir. */
TEST(init_ndir_map_failure_keeps_wdir)
{
    int16_t   asid = NEW_ASID;
    status_$t status = 0;

    reset();
    rights_results[0] = 1;
    rights_results[1] = 1;
    map_status[1] = 0x00050004;
    NAME_$INIT_ASID(&asid, &status);

    ASSERT_EQ(2, map_calls);
    ASSERT_EQ(0x80050004u, (uint32_t)status);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[NEW_ASID], &WDIR_UID));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.ndir_uid[NEW_ASID], &OLD_UID));
    ASSERT_EQ(1, exit_super_calls);
}

/* ------------------------------------------------------------------ */
/* NAME_$FORK                                                           */
/* ------------------------------------------------------------------ */

TEST(fork_copies_uids_and_mapped_info)
{
    int16_t parent = CUR_ASID;
    int16_t child  = NEW_ASID;
    uint8_t *p;
    int i;

    reset();
    p = (uint8_t *)&NAME_$DATA.wdir_mapped_info[CUR_ASID];
    for (i = 0; i < (int)sizeof(name_$mapped_info_t); i++) p[i] = (uint8_t)(0x10 + i);
    p = (uint8_t *)&NAME_$DATA.ndir_mapped_info[CUR_ASID];
    for (i = 0; i < (int)sizeof(name_$mapped_info_t); i++) p[i] = (uint8_t)(0x40 + i);

    NAME_$FORK(&parent, &child);

    ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[NEW_ASID], &WDIR_UID));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.ndir_uid[NEW_ASID], &NDIR_UID));
    ASSERT_EQ(0, memcmp(&NAME_$DATA.wdir_mapped_info[NEW_ASID],
                        &NAME_$DATA.wdir_mapped_info[CUR_ASID],
                        sizeof(name_$mapped_info_t)));
    ASSERT_EQ(0, memcmp(&NAME_$DATA.ndir_mapped_info[NEW_ASID],
                        &NAME_$DATA.ndir_mapped_info[CUR_ASID],
                        sizeof(name_$mapped_info_t)));
    /* the parent's cells are untouched */
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[CUR_ASID], &WDIR_UID));
    ASSERT_EQ(0x10, ((uint8_t *)&NAME_$DATA.wdir_mapped_info[CUR_ASID])[0]);
    ASSERT_EQ(0, enter_super_calls);
}

/* ------------------------------------------------------------------ */
/* NAME_$FREE_ASID                                                      */
/* ------------------------------------------------------------------ */

TEST(free_unmaps_both_and_resets_to_node_uid)
{
    int16_t asid = NEW_ASID;

    reset();
    NAME_$FREE_ASID(&asid);

    ASSERT_EQ(2, unmap_calls);
    ASSERT_EQ(NEW_ASID, unmap_asid_seen[0]);
    ASSERT_EQ(NEW_ASID, unmap_asid_seen[1]);
    ASSERT_EQ((uintptr_t)&NAME_$DATA.wdir_mapped_info[NEW_ASID], (uintptr_t)unmap_info_seen[0]);
    ASSERT_EQ((uintptr_t)&NAME_$DATA.ndir_mapped_info[NEW_ASID], (uintptr_t)unmap_info_seen[1]);
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.wdir_uid[NEW_ASID], &NODE_UID));
    ASSERT_EQ(1, uid_eq(&NAME_$DATA.ndir_uid[NEW_ASID], &NODE_UID));
    ASSERT_EQ(1, enter_super_calls);
    ASSERT_EQ(1, exit_super_calls);
    ASSERT_EQ(0, super_depth);
}

int main(void)
{
    printf("NAME_$INIT_ASID / NAME_$FORK / NAME_$FREE_ASID tests\n");
    RUN_TEST(init_passes_the_three_constant_cells);
    RUN_TEST(init_maps_and_stores_both_dirs);
    RUN_TEST(init_no_rights_is_ok_and_leaves_uids);
    RUN_TEST(init_wdir_map_failure_sets_bit31_and_skips_ndir);
    RUN_TEST(init_ndir_map_failure_keeps_wdir);
    RUN_TEST(fork_copies_uids_and_mapped_info);
    RUN_TEST(free_unmaps_both_and_resets_to_node_uid);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
