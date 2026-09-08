/*
 * asknode/test/test_info.c - ASKNODE_$INFO (0x00E64598)
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

#include "../info.c"

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

TEST(info_forwards_its_five_arguments)
{
    reset();
    ASKNODE_$INFO(&req_type, &node_id, &param, result, &status);

    ASSERT_EQ(1, ii_calls);
    ASSERT_EQ(1, ii_req_type == &req_type);   /* 0x00E645B4, argument 1 */
    ASSERT_EQ(1, ii_node_id  == &node_id);    /* 0x00E645B0, argument 2 */
    ASSERT_EQ(1, ii_param    == &param);      /* 0x00E645A8, argument 4 */
    ASSERT_EQ(1, ii_result   == result);      /* 0x00E645A0, argument 6 */
    ASSERT_EQ(1, ii_status   == &status);     /* 0x00E6459C, argument 7 */
}

TEST(info_supplies_both_pool_constants)
{
    reset();
    ASKNODE_$INFO(&req_type, &node_id, &param, result, &status);

    /* 0x00E645AC "pea (0x12,PC)" -> 0x00E645C0, bytes ff ff ff ff.
     * INTERNET_INFO's request-0x1F arm compares "*req_len != -1" twice to
     * decide whether to re-derive a route with DIR_$FIND_NET and retry; a
     * zero cell would take the "no retry" branch every time. */
    ASSERT_EQ(1, ii_req_len != NULL);
    ASSERT_EQ(-1, *ii_req_len);

    /* 0x00E645A4 "pea (0x18,PC)" -> 0x00E645BE, bytes 00 98: the reply-data
     * ceiling INTERNET_INFO hands PKT_$SAR_INTERNET. */
    ASSERT_EQ(1, ii_resp_len != NULL);
    ASSERT_EQ(0x0098, *ii_resp_len);
}

/* ============================================================================ */

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("ASKNODE_$INFO tests\n");

    RUN_TEST(info_forwards_its_five_arguments);
    RUN_TEST(info_supplies_both_pool_constants);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
