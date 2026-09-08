/*
 * asknode/test/test_get_info.c - ASKNODE_$GET_INFO (0x00E645C4)
 *
 * A thin wrapper around ASKNODE_$INTERNET_INFO whose only substance is the
 * argument order and the constant(s) it supplies from the two-cell pool that
 * sits between ASKNODE_$INFO's "rts" and ASKNODE_$GET_INFO's "link.w".
 * `gsk read 0xE645B0 32`:
 *
 *   00e645b0  2f 2e 00 0c 2f 2e 00 08  61 30 4e 5e 4e 75 00 98
 *                                                        ^0xE645BE  = 0x0098
 *   00e645c0  ff ff ff ff 4e 56 00 00  2f 2e 00 1c 2f 2e 00 18
 *             ^0xE645C0 = 0xFFFFFFFF  ^0xE645C4 = GET_INFO's entry
 *
 * Beads source-6f9k and source-dgfb: the tree used to declare both cells as
 * zero-initialised statics, which turned the -1 request-length constant into 0
 * (disabling INTERNET_INFO's DIR_$FIND_NET retry for request 0x1F) and the
 * 0x98-byte reply ceiling into a zero-length reply buffer.  The pool is one
 * shared object in the image; each C file carries its own read-only copy under
 * the same name, which is why the two functions are tested in separate
 * programs.
 */

#include <stdio.h>
#include <string.h>

#include "asknode/asknode_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-46s ", #name);                                            \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed == 0) { printf("PASSED\n"); }                      \
    } while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected %lld, got %lld\n",                \
                   __LINE__, _e, _a);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * The mocked callee
 * ============================================================================ */

static int        ii_calls;
static uint16_t  *ii_req_type;
static uint32_t  *ii_node_id;
static int32_t   *ii_req_len;
static uid_t     *ii_param;
static uint16_t  *ii_resp_len;
static uint32_t  *ii_result;
static status_$t *ii_status;

uint32_t ASKNODE_$INTERNET_INFO(uint16_t *req_type, uint32_t *node_id,
                                int32_t *req_len, uid_t *param,
                                uint16_t *resp_len, uint32_t *result,
                                status_$t *status)
{
    ii_calls++;
    ii_req_type = req_type;
    ii_node_id  = node_id;
    ii_req_len  = req_len;
    ii_param    = param;
    ii_resp_len = resp_len;
    ii_result   = result;
    ii_status   = status;
    return 0;
}

/* ============================================================================
 * The code under test
 * ============================================================================ */

#include "../get_info.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

static uint16_t  req_type;
static uint32_t  node_id;
static uid_t     param;
static uint32_t  result[64];
static status_$t status;

static void reset(void)
{
    ii_calls = 0;
    ii_req_type = NULL; ii_node_id = NULL; ii_req_len = NULL;
    ii_param = NULL; ii_resp_len = NULL; ii_result = NULL; ii_status = NULL;
    req_type = 0x1F;
    node_id  = 0x00012345u;
    param.high = 0x11111111u;
    param.low  = 0x22222222u;
    memset(result, 0, sizeof(result));
    status = 0;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(get_info_forwards_six_arguments)
{
    uint16_t resp_len = 0x0040;

    reset();
    ASKNODE_$GET_INFO(&req_type, &node_id, &param, &resp_len, result, &status);

    ASSERT_EQ(1, ii_calls);
    ASSERT_EQ(1, ii_req_type == &req_type);   /* 0x00E645E0, argument 1 */
    ASSERT_EQ(1, ii_node_id  == &node_id);    /* 0x00E645DC, argument 2 */
    ASSERT_EQ(1, ii_param    == &param);      /* 0x00E645D4, argument 4 */
    ASSERT_EQ(1, ii_result   == result);      /* 0x00E645CC, argument 6 */
    ASSERT_EQ(1, ii_status   == &status);     /* 0x00E645C8, argument 7 */

    /* Unlike ASKNODE_$INFO, argument 5 is the caller's own cell
     * ("move.l (0x14,A6),-(SP)" at 0x00E645D0). */
    ASSERT_EQ(1, ii_resp_len == &resp_len);
    ASSERT_EQ(0x0040, *ii_resp_len);
}

TEST(get_info_supplies_the_shared_req_len_constant)
{
    uint16_t resp_len = 0x0040;

    reset();
    ASKNODE_$GET_INFO(&req_type, &node_id, &param, &resp_len, result, &status);

    /* 0x00E645D8 "pea (-0x1a,PC)" -> 0x00E645C0, the same cell ASKNODE_$INFO
     * points at from 0x00E645AC, and the same -1 that keeps INTERNET_INFO's
     * request-0x1F retry alive. */
    ASSERT_EQ(1, ii_req_len != NULL);
    ASSERT_EQ(-1, *ii_req_len);
}

/* ============================================================================ */

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("ASKNODE_$GET_INFO tests\n");

    RUN_TEST(get_info_forwards_six_arguments);
    RUN_TEST(get_info_supplies_the_shared_req_len_constant);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
