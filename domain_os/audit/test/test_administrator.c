/*
 * audit/test/test_administrator.c - AUDIT_$ADMINISTRATOR (0x00E714B6).
 *
 * The property under test is the name NAME_$RESOLVE is handed.  The image
 * pushes two in-code cells:
 *
 *   00e714ca  pea (0x52,PC)    -> 0x00E7151E, the length word 0x0010
 *   00e714ce  pea (0x50,PC)    -> 0x00E71520, sixteen characters
 *
 *   00e71520  60 6e 6f 64 65 5f 64 61  74 61 2f 61 75 64 69 74
 *              `  n  o  d  e  _  d  a   t  a  /  a  u  d  i  t
 *
 * so the name is "`node_data/audit" - one backquote, sixteen bytes, counted
 * rather than terminated - and not the seventeen-byte "//node_data/audit" the
 * earlier C built with an invented length variable.
 *
 * Also pinned: the rights test is the whole longword against 2
 * (0x00E71504 cmpi.l #0x2,D0), the true answer is the Pascal 0xFF
 * (0x00E71510 st D2b), and a failed resolve reports 0x0030000C without
 * calling ACL_$RIGHTS (0x00E714DC / 0x00E714E0).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

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
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Globals and mocks
 * ============================================================================ */

#include "audit/audit_internal.h"
#include "name/name.h"
#include "acl/acl.h"

/* --- NAME_$RESOLVE ------------------------------------------------------ */

static int resolve_calls;
static char resolve_path[64];
static int16_t resolve_len;
static status_$t resolve_status;
static uid_t resolve_uid;

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid,
                   status_$t *status_ret)
{
    resolve_calls++;
    resolve_len = *path_len;
    memset(resolve_path, 0, sizeof resolve_path);
    if (resolve_len > 0 && (size_t)resolve_len < sizeof resolve_path) {
        memcpy(resolve_path, path, (size_t)resolve_len);
    }
    *resolved_uid = resolve_uid;
    *status_ret = resolve_status;
}

/* --- ACL_$RIGHTS -------------------------------------------------------- */

static int rights_calls;
static boolean rights_ignore_super;
static uint32_t rights_mask;
static int16_t rights_opts;
static uint32_t rights_result;

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *options, status_$t *status_ret)
{
    (void)uid;
    (void)status_ret;
    rights_calls++;
    rights_ignore_super = *ignore_super;
    rights_mask = *required_mask;
    rights_opts = *options;
    return rights_result;
}

#include "../administrator.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static void reset(void)
{
    resolve_calls = 0;
    resolve_len = -1;
    resolve_status = status_$ok;
    memset(&resolve_uid, 0, sizeof resolve_uid);
    rights_calls = 0;
    rights_result = 0;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(resolves_the_sixteen_byte_backquote_name)
{
    status_$t st = 0;

    reset();
    rights_result = 2;

    (void)AUDIT_$ADMINISTRATOR(&st);

    ASSERT_EQ(1, resolve_calls);
    ASSERT_EQ(16, resolve_len);
    ASSERT_TRUE(memcmp(resolve_path, "`node_data/audit", 16) == 0);
    /* the first byte is a backquote, not a slash */
    ASSERT_EQ(0x60, (unsigned char)resolve_path[0]);
}

/* The in-code cells themselves. */
TEST(the_path_cells_hold_the_image_bytes)
{
    static const uint8_t image[16] = {
        0x60, 0x6e, 0x6f, 0x64, 0x65, 0x5f, 0x64, 0x61,
        0x74, 0x61, 0x2f, 0x61, 0x75, 0x64, 0x69, 0x74
    };

    ASSERT_EQ(16, sizeof audit_$admin_path_00e71520);
    ASSERT_TRUE(memcmp(audit_$admin_path_00e71520, image, 16) == 0);
    ASSERT_EQ(0x0010, audit_$admin_path_len_00e7151e);
}

/* 0x00E71504-0x00E71510: exactly 2 means yes, and yes is 0xFF. */
TEST(rights_of_two_yields_pascal_true)
{
    status_$t st = 0;
    int8_t answer;

    reset();
    rights_result = 2;

    answer = AUDIT_$ADMINISTRATOR(&st);

    ASSERT_EQ(1, rights_calls);
    ASSERT_EQ(0, rights_ignore_super);      /* 0x00E71498, FALSE */
    ASSERT_EQ(2u, rights_mask);             /* 0x00E71530 */
    ASSERT_EQ(1, rights_opts);              /* 0x00E71496 */
    ASSERT_EQ((int8_t)0xFF, answer);
}

TEST(any_other_rights_value_yields_false)
{
    status_$t st = 0;

    reset();
    rights_result = 3;
    ASSERT_EQ(0, AUDIT_$ADMINISTRATOR(&st));

    reset();
    rights_result = 0;
    ASSERT_EQ(0, AUDIT_$ADMINISTRATOR(&st));

    /* the longword is compared whole, so 2 in the low word is not enough */
    reset();
    rights_result = 0x00010002u;
    ASSERT_EQ(0, AUDIT_$ADMINISTRATOR(&st));
}

/* 0x00E714DC tst.l (A2) / bne: a failed resolve short-circuits. */
TEST(a_failed_resolve_reports_the_audit_code)
{
    status_$t st = 0;
    int8_t answer;

    reset();
    resolve_status = 0x00120005;

    answer = AUDIT_$ADMINISTRATOR(&st);

    ASSERT_EQ(0, answer);
    ASSERT_EQ(0, rights_calls);
    ASSERT_EQ(0x0030000C, (uint32_t)st);
}

int main(void)
{
    printf("AUDIT_$ADMINISTRATOR tests\n");
    RUN_TEST(resolves_the_sixteen_byte_backquote_name);
    RUN_TEST(the_path_cells_hold_the_image_bytes);
    RUN_TEST(rights_of_two_yields_pascal_true);
    RUN_TEST(any_other_rights_value_yields_false);
    RUN_TEST(a_failed_resolve_reports_the_audit_code);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
