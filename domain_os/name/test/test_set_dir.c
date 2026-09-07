/*
 * name/test/test_set_dir.c - unit tests for NAME_$SET_WDIR (0x00E4A3D0),
 * NAME_$SET_WDIRUS (0x00E58670) and NAME_$SET_NDIRUS (0x00E587A0)
 *
 * The real name/set_dir.c is #included below and driven through mocks of its
 * callees.  The behaviours exercised are the ones bead source-0bo left as
 * TODOs:
 *
 *   - the ACL_$RIGHTS arguments, which the image passes as the addresses of
 *     four pooled constants at 0x00E58796..0x00E5879C (boolean TRUE, rights
 *     mask 1, option flags 1, audit length 8);
 *   - the audit trailer at 0x00E58744-0x00E58786, which was not implemented
 *     at all: event UID {0x00040027, 0} for the working directory and
 *     {0x00040028, 0} for the naming directory, the event flag word (1 on
 *     failure, 0 on success), and the 8-byte data being the UID itself;
 *   - that the "already this directory" fast path (0x00E5869A) returns
 *     without touching the ACL, the mapping or the audit log;
 *   - that a mapping failure sets bit 31 of the status (`bset.b #0x7,(A3)`
 *     at 0x00E58720) and leaves the stored UID alone;
 *   - that the two routines address the ndir tables (+0x040 / +0x3E0) and the
 *     wdir tables (+0x5B0 / +0x950) respectively, indexed by PROC1_$AS_ID
 *     with no bias.
 */

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                    */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-44s ", #name);          \
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
#include "audit/audit.h"

/* ------------------------------------------------------------------ */
/* Globals the code under test refers to                                */
/* ------------------------------------------------------------------ */

name_$data_t NAME_$DATA;
uid_t        UID_$NIL;
uint16_t     PROC1_$AS_ID;
uint16_t     PROC1_$CURRENT;
int8_t       AUDIT_$ENABLED;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int enter_super_calls;
static int exit_super_calls;

void ACL_$ENTER_SUPER(void) { enter_super_calls++; }
void ACL_$EXIT_SUPER(void)  { exit_super_calls++; }

static int         acl_calls;
static uint32_t    acl_result;
static status_$t   acl_status;
static const boolean *acl_arg2;
static uint32_t    acl_rights_seen;
static int16_t     acl_opts_seen;
static uid_t       acl_uid_seen;

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status)
{
    acl_calls++;
    acl_uid_seen    = *uid;
    acl_arg2        = ignore_super;
    acl_rights_seen = *required_mask;
    acl_opts_seen   = *option_flags;
    *status = acl_status;
    return acl_result;
}

static int       convert_calls;
static status_$t convert_output;

void NAME_CONVERT_ACL_STATUS(status_$t *status)
{
    convert_calls++;
    *status = convert_output;
}

static int                  unmap_calls;
static int16_t              unmap_asid;
static name_$mapped_info_t *unmap_info;

void name_$unmap_dir_buffers(int16_t asid, name_$mapped_info_t *info)
{
    unmap_calls++;
    unmap_asid = asid;
    unmap_info = info;
}

static int                  map_calls;
static int16_t              map_asid;
static name_$mapped_info_t *map_info;
static uid_t                map_uid;
static status_$t            map_status;

boolean name_$map_dir(uid_t *uidp, int16_t asid, name_$mapped_info_t *info,
                      status_$t *status)
{
    map_calls++;
    map_uid  = *uidp;
    map_asid = asid;
    map_info = info;
    *status  = map_status;
    return (*status == status_$ok) ? true : false;
}

static int       resolve_calls;
static uid_t     resolve_uid;
static status_$t resolve_status;

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *uid_out,
                   status_$t *status)
{
    (void)path; (void)path_len;
    resolve_calls++;
    *uid_out = resolve_uid;
    *status  = resolve_status;
}

static int       audit_calls;
static uid_t     audit_event_uid;
static uint16_t  audit_flags;
static uint32_t  audit_status;
static uid_t     audit_data;
static uint16_t  audit_len;

void AUDIT_$LOG_EVENT(uid_t *event_uid, uint16_t *event_flags,
                      uint32_t *status, char *data, uint16_t *data_len)
{
    audit_calls++;
    audit_event_uid = *event_uid;
    audit_flags     = *event_flags;
    audit_status    = *status;
    audit_len       = *data_len;
    memcpy(&audit_data, data, sizeof(audit_data));
}

/* ------------------------------------------------------------------ */
/* The code under test                                                  */
/* ------------------------------------------------------------------ */

#include "../set_dir.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

#define TEST_ASID   5

static uid_t new_uid;

static void reset(void)
{
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    memset(&UID_$NIL, 0, sizeof(UID_$NIL));

    PROC1_$AS_ID   = TEST_ASID;
    PROC1_$CURRENT = 3;
    AUDIT_$ENABLED = 0;

    enter_super_calls = exit_super_calls = 0;
    acl_calls = convert_calls = unmap_calls = map_calls = 0;
    resolve_calls = audit_calls = 0;
    acl_result = -1;                /* access granted */
    acl_status = status_$ok;
    convert_output = (status_$t)0x000E0014;
    map_status = status_$ok;
    resolve_status = status_$ok;

    new_uid.high = 0x11223344u;
    new_uid.low  = 0x55667788u;
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/* 0x00E5869A-0x00E586A4: two `cmpm.l`, then `clr.l (A3)` and out. */
TEST(wdirus_already_set_is_a_no_op)
{
    status_$t st = (status_$t)0x7FFFFFFF;

    reset();
    NAME_$DATA.wdir_uid[TEST_ASID] = new_uid;

    NAME_$SET_WDIRUS(&new_uid, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, enter_super_calls);
    ASSERT_EQ(0, acl_calls);
    ASSERT_EQ(0, unmap_calls);
    ASSERT_EQ(0, map_calls);
    ASSERT_EQ(0, audit_calls);
}

/* The four pooled constants at 0x00E58796..0x00E5879C. */
TEST(acl_rights_gets_the_pooled_constants)
{
    status_$t st = status_$ok;

    reset();
    NAME_$SET_WDIRUS(&new_uid, &st);

    ASSERT_EQ(1, acl_calls);
    ASSERT_EQ(new_uid.high, acl_uid_seen.high);
    ASSERT_EQ(new_uid.low,  acl_uid_seen.low);
    /* 0x00E5879A holds a byte 0xFF - a Domain TRUE. */
    ASSERT_EQ(0xFF, (unsigned char)*acl_arg2);
    /* 0x00E5879C holds the longword 1. */
    ASSERT_EQ(1, acl_rights_seen);
    /* 0x00E58796 holds the word 1. */
    ASSERT_EQ(1, acl_opts_seen);
}

/* 0x00E586C8-0x00E586D6: `tst.l D0` + `seq` + `bpl` - zero means denied. */
TEST(acl_denial_converts_the_status_and_maps_nothing)
{
    status_$t st = status_$ok;

    reset();
    acl_result = 0;

    NAME_$SET_WDIRUS(&new_uid, &st);

    ASSERT_EQ(1, convert_calls);
    ASSERT_EQ((status_$t)0x000E0014, st);
    ASSERT_EQ(0, unmap_calls);
    ASSERT_EQ(0, map_calls);
    ASSERT_EQ(1, enter_super_calls);
    ASSERT_EQ(1, exit_super_calls);
    /* The stored UID must be untouched. */
    ASSERT_EQ(0, NAME_$DATA.wdir_uid[TEST_ASID].high);
}

/* 0x00E586DA-0x00E58738: unmap, map, then store into the per-ASID slot. */
TEST(wdirus_success_stores_the_uid_in_the_wdir_tables)
{
    status_$t st = (status_$t)0x7FFFFFFF;

    reset();
    NAME_$SET_WDIRUS(&new_uid, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, unmap_calls);
    ASSERT_EQ(TEST_ASID, unmap_asid);
    ASSERT_EQ((unsigned long)&NAME_$DATA.wdir_mapped_info[TEST_ASID],
              (unsigned long)unmap_info);
    ASSERT_EQ(1, map_calls);
    ASSERT_EQ(TEST_ASID, map_asid);
    ASSERT_EQ((unsigned long)&NAME_$DATA.wdir_mapped_info[TEST_ASID],
              (unsigned long)map_info);
    ASSERT_EQ(new_uid.high, map_uid.high);
    ASSERT_EQ(new_uid.high, NAME_$DATA.wdir_uid[TEST_ASID].high);
    ASSERT_EQ(new_uid.low,  NAME_$DATA.wdir_uid[TEST_ASID].low);
    /* the naming-directory tables must not have moved */
    ASSERT_EQ(0, NAME_$DATA.ndir_uid[TEST_ASID].high);
}

TEST(ndirus_success_stores_the_uid_in_the_ndir_tables)
{
    status_$t st = (status_$t)0x7FFFFFFF;

    reset();
    NAME_$SET_NDIRUS(&new_uid, &st);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((unsigned long)&NAME_$DATA.ndir_mapped_info[TEST_ASID],
              (unsigned long)map_info);
    ASSERT_EQ(new_uid.high, NAME_$DATA.ndir_uid[TEST_ASID].high);
    ASSERT_EQ(new_uid.low,  NAME_$DATA.ndir_uid[TEST_ASID].low);
    ASSERT_EQ(0, NAME_$DATA.wdir_uid[TEST_ASID].high);
}

/* 0x00E58720 `bset.b #0x7,(A3)` - bit 7 of the first byte is bit 31. */
TEST(map_failure_sets_bit_31_and_leaves_the_uid)
{
    status_$t st = status_$ok;

    reset();
    map_status = (status_$t)0x00040005;

    NAME_$SET_WDIRUS(&new_uid, &st);

    ASSERT_EQ((status_$t)0x80040005u, st);
    ASSERT_EQ(0, NAME_$DATA.wdir_uid[TEST_ASID].high);
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E58744 `tst.b AUDIT_$ENABLED` + `bpl`: a Domain boolean. */
TEST(audit_is_skipped_unless_enabled)
{
    status_$t st = status_$ok;

    reset();
    AUDIT_$ENABLED = 0;
    NAME_$SET_WDIRUS(&new_uid, &st);
    ASSERT_EQ(0, audit_calls);

    reset();
    AUDIT_$ENABLED = 1;             /* set but positive - still skipped */
    NAME_$SET_WDIRUS(&new_uid, &st);
    ASSERT_EQ(0, audit_calls);
}

/* 0x00E5874C-0x00E58786 for the working directory. */
TEST(wdir_audit_event_is_0x00040027)
{
    status_$t st = status_$ok;

    reset();
    AUDIT_$ENABLED = -1;

    NAME_$SET_WDIRUS(&new_uid, &st);

    ASSERT_EQ(1, audit_calls);
    ASSERT_EQ(0x00040027u, audit_event_uid.high);
    ASSERT_EQ(0u, audit_event_uid.low);
    ASSERT_EQ(0, audit_flags);              /* success */
    ASSERT_EQ(8, audit_len);                /* the cell at 0x00E58798 */
    ASSERT_EQ(new_uid.high, audit_data.high);
    ASSERT_EQ(new_uid.low,  audit_data.low);
}

/* 0x00E58888 stores 0x28 instead of 0x27. */
TEST(ndir_audit_event_is_0x00040028)
{
    status_$t st = status_$ok;

    reset();
    AUDIT_$ENABLED = -1;

    NAME_$SET_NDIRUS(&new_uid, &st);

    ASSERT_EQ(1, audit_calls);
    ASSERT_EQ(0x00040028u, audit_event_uid.high);
    ASSERT_EQ(0u, audit_event_uid.low);
}

/* 0x00E58766-0x00E58772: the event flag word is 1 for any non-zero status. */
TEST(audit_flag_is_one_on_failure)
{
    status_$t st = status_$ok;

    reset();
    AUDIT_$ENABLED = -1;
    map_status = (status_$t)0x00040005;

    NAME_$SET_WDIRUS(&new_uid, &st);

    ASSERT_EQ(1, audit_calls);
    ASSERT_EQ(1, audit_flags);
    ASSERT_EQ(0x80040005u, audit_status);
}

/* An ACL denial still reaches the audit log - the `bra` at 0x00E586D8 lands
 * on the ACL_$EXIT_SUPER at 0x00E5873E, not on the epilogue. */
TEST(acl_denial_is_audited)
{
    status_$t st = status_$ok;

    reset();
    AUDIT_$ENABLED = -1;
    acl_result = 0;

    NAME_$SET_WDIRUS(&new_uid, &st);

    ASSERT_EQ(1, audit_calls);
    ASSERT_EQ(1, audit_flags);
}

/* 0x00E4A3E8-0x00E4A3FA */
TEST(set_wdir_resolves_then_delegates)
{
    status_$t st = status_$ok;

    reset();
    resolve_uid.high = 0xAABBCCDDu;
    resolve_uid.low  = 0xEEFF0011u;

    NAME_$SET_WDIR("x", NULL, &st);

    ASSERT_EQ(1, resolve_calls);
    ASSERT_EQ(1, map_calls);
    ASSERT_EQ(0xAABBCCDDu, NAME_$DATA.wdir_uid[TEST_ASID].high);
}

/* 0x00E4A3F0 `tst.l (A2)` + `bne` - a failed resolve never sets anything. */
TEST(set_wdir_stops_when_resolve_fails)
{
    status_$t st = status_$ok;

    reset();
    resolve_status = (status_$t)0x000E0007;

    NAME_$SET_WDIR("x", NULL, &st);

    ASSERT_EQ(1, resolve_calls);
    ASSERT_EQ(0, acl_calls);
    ASSERT_EQ(0, map_calls);
    ASSERT_EQ((status_$t)0x000E0007, st);
}

int main(void)
{
    printf("NAME_$SET_WDIR / NAME_$SET_WDIRUS / NAME_$SET_NDIRUS tests\n");

    RUN_TEST(wdirus_already_set_is_a_no_op);
    RUN_TEST(acl_rights_gets_the_pooled_constants);
    RUN_TEST(acl_denial_converts_the_status_and_maps_nothing);
    RUN_TEST(wdirus_success_stores_the_uid_in_the_wdir_tables);
    RUN_TEST(ndirus_success_stores_the_uid_in_the_ndir_tables);
    RUN_TEST(map_failure_sets_bit_31_and_leaves_the_uid);
    RUN_TEST(audit_is_skipped_unless_enabled);
    RUN_TEST(wdir_audit_event_is_0x00040027);
    RUN_TEST(ndir_audit_event_is_0x00040028);
    RUN_TEST(audit_flag_is_one_on_failure);
    RUN_TEST(acl_denial_is_audited);
    RUN_TEST(set_wdir_resolves_then_delegates);
    RUN_TEST(set_wdir_stops_when_resolve_fails);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
