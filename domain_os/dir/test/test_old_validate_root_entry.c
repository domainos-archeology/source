/*
 * dir/test/test_old_validate_root_entry.c - unit tests for
 * DIR_$OLD_VALIDATE_ROOT_ENTRY (0x00E580C4)
 *
 * Bead source-kurv.  The behaviours pinned here: the naming-server lookup is
 * made against NAME_$CANNED_REP_ROOT_UID (0x00E173FC) with the CALLER's
 * status cell and returned as-is (0x00E58100-0x00E5811E); the UID compare is
 * followed by the extra-longword compare (0x00E58134) and, on a UID
 * mismatch, by the "newer generation" test at 0x00E58140-0x00E58170; and the
 * repair path re-adds the UNMAPPED name with type word 0, the replicated
 * entry's own UID and extra, ending in 0xE0007 or 0xE0023.
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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

name_$data_t NAME_$DATA;
uid_t NAME_$CANNED_REP_ROOT_UID = { 0x00000404u, 0x00000000u };  /* 0xE173FC */

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int              gen_calls;
static uid_t           *gen_dir_seen;
static status_$t        gen_status;
static dir_$old_entry_t gen_entry;
void name_$old_get_entry_nonroot(uid_t *dir_uid, char *name, uint16_t name_len,
                                 dir_$old_entry_t *entry_ret,
                                 status_$t *status_ret)
{
    gen_calls++;
    gen_dir_seen = dir_uid;
    (void)name; (void)name_len;
    *(dir_$old_entry_t *)entry_ret = gen_entry;
    *status_ret = gen_status;
}

static int              rem_calls;
static uid_t           *rem_dir_seen;
static status_$t       *rem_status_cell;
static status_$t        rem_status;
static dir_$rep_entry_t rem_entry;
void REM_NAME_$GET_ENTRY(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret)
{
    rem_calls++;
    rem_dir_seen = dir_uid;
    rem_status_cell = status_ret;
    (void)name; (void)name_len;
    *(dir_$rep_entry_t *)entry_ret = rem_entry;
    *status_ret = rem_status;
}

static int       drop_calls;
static uid_t    *drop_dir_seen;
static uint16_t  drop_type_seen;
static status_$t drop_status;
void name_$old_drop_entry(uid_t *dir_uid, char *name, uint16_t name_len,
                          uint16_t type, void *result, status_$t *status_ret)
{
    drop_calls++;
    drop_dir_seen = dir_uid;
    drop_type_seen = type;
    (void)name; (void)name_len; (void)result;
    *status_ret = drop_status;
}

static int      uc_calls;
static int16_t *uc_max_seen;
static char     uc_in_first;
static int16_t  uc_in_len_seen;
static int16_t  uc_out_len;
void UNMAP_CASE(char *name, int16_t *name_len, char *output,
                int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    uc_calls++;
    uc_max_seen = max_out_len;
    uc_in_first = name[0];
    uc_in_len_seen = *name_len;
    output[0] = 'u';
    *out_len = uc_out_len;
    *truncated = 0;
}

static int       add_calls;
static uid_t    *add_dir_seen;
static uint16_t  add_type_seen;
static uint16_t  add_len_seen;
static uid_t    *add_uid_seen;
static uint32_t  add_flags_seen;
static char      add_name_first;
static status_$t add_status;
void name_$old_add_entry(uid_t *dir_uid, uint16_t type, char *name,
                         uint16_t name_len, uid_t *file_uid,
                         uint32_t flags, status_$t *status_ret)
{
    add_calls++;
    add_dir_seen = dir_uid;
    add_type_seen = type;
    add_len_seen = name_len;
    add_uid_seen = file_uid;
    add_flags_seen = flags;
    add_name_first = name[0];
    *status_ret = add_status;
}

/* the shared 0xE544AE cell (defined by name/validate_leaf.c in the kernel) */
const int16_t name_$leaf_max_len_00e544ae = 0x0020;

#include "../old_validate_root_entry.c"

/* ------------------------------------------------------------------ */

static status_$t st;
static uint16_t  name_len;
static char      the_name[] = "somenode";

static const uid_t ROOT = { 0x0BADF00Du, 0x0BADBEEFu };

static void reset(void)
{
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    NAME_$ROOT_UID = ROOT;
    memset(&gen_entry, 0, sizeof(gen_entry));
    memset(&rem_entry, 0, sizeof(rem_entry));
    gen_entry.type = 2;
    gen_entry.uid.high = 0x01020304u;
    gen_entry.uid.low  = 0x000AAAAAu;
    gen_entry.extra = 0x11112222u;
    rem_entry.name_len = 4;
    memcpy(rem_entry.name, "ABCD", 4);
    rem_entry.uid.high = 0x01020304u;
    rem_entry.uid.low  = 0x000AAAAAu;
    rem_entry.extra = 0x11112222u;
    gen_calls = 0; gen_status = status_$ok; gen_dir_seen = NULL;
    rem_calls = 0; rem_status = status_$ok; rem_dir_seen = NULL;
    rem_status_cell = NULL;
    drop_calls = 0; drop_status = status_$ok; drop_type_seen = 0xFFFF;
    drop_dir_seen = NULL;
    uc_calls = 0; uc_max_seen = NULL; uc_in_first = 0;
    uc_in_len_seen = 0; uc_out_len = 6;
    add_calls = 0; add_status = status_$ok; add_type_seen = 0xFFFF;
    add_len_seen = 0xFFFF; add_uid_seen = NULL; add_flags_seen = 0;
    add_name_first = 0; add_dir_seen = NULL;
    st = 0x5A5A5A5A;
    name_len = 8;
}

/* 0x00E580EA / 0x00E5810A: two different well-known UIDs, no local copies. */
TEST(the_two_lookups_use_the_two_canned_uids)
{
    reset();
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_TRUE(gen_dir_seen == &NAME_$ROOT_UID);
    ASSERT_TRUE(rem_dir_seen == &NAME_$CANNED_REP_ROOT_UID);
    ASSERT_TRUE(rem_status_cell == &st);
}

/* 0x00E580F8: a local miss stops before the server is asked. */
TEST(a_local_miss_returns_immediately)
{
    reset();
    gen_status = status_$naming_name_not_found;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
    ASSERT_EQ(0, rem_calls);
}

/*
 * 0x00E5811A: the server's status is returned exactly as it stands - there
 * is no name_not_found -> cache_entry_stale mapping.
 */
TEST(the_servers_status_is_returned_unmapped)
{
    reset();
    rem_status = status_$naming_name_not_found;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
    ASSERT_EQ(0, drop_calls);
}

/* Matching UID and extra: status 0 (0x00E58172). */
TEST(an_entry_that_matches_is_ok)
{
    reset();
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, drop_calls);
}

/* 0x00E58134: the same UID with a different extra longword is stale. */
TEST(a_matching_uid_with_a_different_extra_is_repaired)
{
    reset();
    rem_entry.extra = 0x33334444u;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(1, drop_calls);
    ASSERT_EQ(status_$naming_cache_entry_stale_and_updated, st);
}

/*
 * 0x00E58140-0x00E58170: a UID mismatch is accepted when both leading bytes
 * are nonzero, the low 20 bits of the UID low words match, and the local
 * high longword is unsigned-greater.
 */
TEST(a_newer_local_generation_is_accepted)
{
    reset();
    rem_entry.uid.high = 0x01020303u;       /* local 0x01020304 is greater */
    rem_entry.uid.low  = 0xFFF00000u | 0x000AAAAAu;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, drop_calls);
}

/* An older local generation is not. */
TEST(an_older_local_generation_is_repaired)
{
    reset();
    rem_entry.uid.high = 0x01020305u;       /* greater than the local one */
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(1, drop_calls);
    ASSERT_EQ(status_$naming_cache_entry_stale_and_updated, st);
}

/* A zero leading byte on either side fails the secondary test. */
TEST(a_zero_leading_uid_byte_fails_the_secondary_test)
{
    reset();
    gen_entry.uid.high = 0x00020304u;
    rem_entry.uid.high = 0x00020303u;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(1, drop_calls);
}

/* Differing low 20 bits fail it too. */
TEST(differing_low_20_bits_fail_the_secondary_test)
{
    reset();
    rem_entry.uid.high = 0x01020303u;
    rem_entry.uid.low  = 0x000BBBBBu;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(1, drop_calls);
}

/* 0x00E58190: a failed drop yields 0xE0022 and stops. */
TEST(a_failed_drop_is_cache_entry_stale)
{
    reset();
    rem_entry.extra = 0x33334444u;
    drop_status = status_$naming_bad_directory;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(status_$naming_cache_entry_stale, st);
    ASSERT_EQ(0, uc_calls);
    ASSERT_EQ(0, add_calls);
}

/*
 * 0x00E5819E / 0x00E581BE: the repair un-maps the replicated name and re-adds
 * it with type word 0, the replicated UID and the replicated extra.
 */
TEST(the_repair_re_adds_the_unmapped_replicated_entry)
{
    reset();
    rem_entry.extra = 0x33334444u;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(1, uc_calls);
    ASSERT_EQ(0x0020, *uc_max_seen);
    /* the case-mapped name the server sent, at rep_entry+0x04 */
    ASSERT_EQ('A', uc_in_first);
    ASSERT_EQ(4, uc_in_len_seen);
    ASSERT_EQ(1, add_calls);
    ASSERT_TRUE(add_dir_seen == &NAME_$ROOT_UID);
    ASSERT_EQ(0, add_type_seen);
    ASSERT_EQ(6, add_len_seen);              /* UNMAP_CASE's output length */
    ASSERT_EQ('u', add_name_first);          /* the UNMAPPED name */
    ASSERT_EQ(rem_entry.uid.high, add_uid_seen->high);
    ASSERT_EQ(rem_entry.uid.low,  add_uid_seen->low);
    ASSERT_EQ(0x33334444u, add_flags_seen);
    ASSERT_EQ(0, drop_type_seen);
    ASSERT_EQ(status_$naming_cache_entry_stale_and_updated, st);
}

/* 0x00E581E6: a failed re-add is 0xE0007, not 0xE0023. */
TEST(a_failed_re_add_is_name_not_found)
{
    reset();
    rem_entry.extra = 0x33334444u;
    add_status = status_$directory_is_full;
    DIR_$OLD_VALIDATE_ROOT_ENTRY(the_name, &name_len, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
}

int main(void)
{
    printf("DIR_$OLD_VALIDATE_ROOT_ENTRY (0x00E580C4) tests\n");

    RUN_TEST(the_two_lookups_use_the_two_canned_uids);
    RUN_TEST(a_local_miss_returns_immediately);
    RUN_TEST(the_servers_status_is_returned_unmapped);
    RUN_TEST(an_entry_that_matches_is_ok);
    RUN_TEST(a_matching_uid_with_a_different_extra_is_repaired);
    RUN_TEST(a_newer_local_generation_is_accepted);
    RUN_TEST(an_older_local_generation_is_repaired);
    RUN_TEST(a_zero_leading_uid_byte_fails_the_secondary_test);
    RUN_TEST(differing_low_20_bits_fail_the_secondary_test);
    RUN_TEST(a_failed_drop_is_cache_entry_stale);
    RUN_TEST(the_repair_re_adds_the_unmapped_replicated_entry);
    RUN_TEST(a_failed_re_add_is_name_not_found);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
