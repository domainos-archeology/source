/*
 * vtoc/test/test_search_volumes.c - VTOC_$SEARCH_VOLUMES (0x00E01BEE).
 *
 * Two properties from the disassembly review:
 *
 *   - SIX volumes are tried, 1..6.  "moveq #0x5,D2" at 0x00E01C06 seeds a
 *     dbf counter, which runs the body six times, and the index in D3 starts
 *     at 1 (0x00E01C08).  The earlier C stopped at 5.
 *
 *   - The "serious error" guard is on the HIGH word of the status.
 *     "tst.w (A3) / bpl" at 0x00E01C2C reads the first word at the status
 *     address, which on the m68k is the module half, so the test is bit 31 of
 *     the longword.  The earlier C tested the low word and called
 *     ast_$validate_uid for the wrong statuses.
 *
 * Also pinned: the dismount-mask skip (0x00E01C10 / 0x00E01C14), the
 * index > 15 bypass (0x00E01C0A), the early return on a zero status
 * (0x00E01C28) and the diskless short circuit (0x00E01BFE).
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

#include "vtoc/vtoc_internal.h"
#include "ast/ast.h"
#include "file/file.h"
#include "network/network.h"

int8_t NETWORK_$REALLY_DISKLESS;
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

#define MAX_TRIES 16
static int lookup_calls;
static uint8_t tried[MAX_TRIES];
static status_$t lookup_status[MAX_TRIES];  /* what each call returns */
static status_$t lookup_default;

void VTOC_$LOOKUP(vtoc_$lookup_req_t *req, status_$t *status_ret)
{
    if (lookup_calls < MAX_TRIES) {
        tried[lookup_calls] = req->vol_idx;
        *status_ret = lookup_status[lookup_calls] ? lookup_status[lookup_calls]
                                                  : lookup_default;
    } else {
        *status_ret = lookup_default;
    }
    lookup_calls++;
}

static int validate_calls;
static uint32_t validate_status_seen;
static uid_t *validate_uid_seen;

status_$t ast_$validate_uid(uid_t *uid, uint32_t flags)
{
    validate_calls++;
    validate_uid_seen = uid;
    validate_status_seen = flags;
    return 0;
}

#include "../search_volumes.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static vtoc_$lookup_req_t req;

static void reset(void)
{
    memset(&req, 0, sizeof req);
    memset(tried, 0, sizeof tried);
    memset(lookup_status, 0, sizeof lookup_status);
    lookup_calls = 0;
    lookup_default = file_$object_not_found;
    validate_calls = 0;
    NETWORK_$REALLY_DISKLESS = 0;
    AST_$DATA.vol_info_count = 0;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* Six passes, indices 1..6. */
TEST(tries_six_volumes_one_through_six)
{
    status_$t st = 0;

    reset();

    VTOC_$SEARCH_VOLUMES(&req, &st);

    ASSERT_EQ(6, lookup_calls);
    ASSERT_EQ(1, tried[0]);
    ASSERT_EQ(2, tried[1]);
    ASSERT_EQ(3, tried[2]);
    ASSERT_EQ(4, tried[3]);
    ASSERT_EQ(5, tried[4]);
    ASSERT_EQ(6, tried[5]);
    ASSERT_EQ(0x000F0001u, (uint32_t)st);
}

/* 0x00E01C28: a zero status stops the walk with that status intact. */
TEST(a_hit_returns_immediately)
{
    status_$t st = 0x7F7F7F7F;

    reset();
    lookup_status[2] = 0;           /* third call falls back to... */
    lookup_default = 0;             /* ...an immediate hit on volume 1 */

    VTOC_$SEARCH_VOLUMES(&req, &st);

    ASSERT_EQ(1, lookup_calls);
    ASSERT_EQ(1, tried[0]);
    ASSERT_EQ(0, (uint32_t)st);
}

/* 0x00E01C10 / 0x00E01C14: a set bit skips that volume entirely. */
TEST(the_dismount_mask_skips_volumes)
{
    status_$t st = 0;

    reset();
    /* volumes 2 and 5 are being dismounted */
    AST_$DATA.vol_info_count = (uint16_t)((1u << 2) | (1u << 5));

    VTOC_$SEARCH_VOLUMES(&req, &st);

    ASSERT_EQ(4, lookup_calls);
    ASSERT_EQ(1, tried[0]);
    ASSERT_EQ(3, tried[1]);
    ASSERT_EQ(4, tried[2]);
    ASSERT_EQ(6, tried[3]);
}

/*
 * The status guard is the HIGH word.  file_$object_not_found (0x000F0001) has
 * a positive high word, so nothing is validated; a status with bit 31 set is
 * handed to ast_$validate_uid whole.
 */
TEST(only_a_negative_high_word_validates_the_uid)
{
    status_$t st = 0;

    reset();
    lookup_default = 0x000F0001;        /* high word 0x000F, positive */
    VTOC_$SEARCH_VOLUMES(&req, &st);
    ASSERT_EQ(0, validate_calls);

    reset();
    /* low word negative, high word positive: still NOT validated */
    lookup_default = 0x0001FFFF;
    VTOC_$SEARCH_VOLUMES(&req, &st);
    ASSERT_EQ(0, validate_calls);

    reset();
    /* high word negative: validated on every one of the six passes */
    lookup_default = (status_$t)0x80010002u;
    VTOC_$SEARCH_VOLUMES(&req, &st);
    ASSERT_EQ(6, validate_calls);
    ASSERT_EQ(0x80010002u, validate_status_seen);
    /* the UID handed over is req + 0x08 ("pea (0x8,A2)" at 0x00E01C32) */
    ASSERT_TRUE(validate_uid_seen == &req.uid);
}

/* 0x00E01BFE: a really diskless node looks at nothing at all. */
TEST(diskless_skips_the_whole_walk)
{
    status_$t st = 0;

    reset();
    NETWORK_$REALLY_DISKLESS = (int8_t)0x80;

    VTOC_$SEARCH_VOLUMES(&req, &st);

    ASSERT_EQ(0, lookup_calls);
    ASSERT_EQ(0x000F0001u, (uint32_t)st);
}

int main(void)
{
    printf("VTOC_$SEARCH_VOLUMES tests\n");
    RUN_TEST(tries_six_volumes_one_through_six);
    RUN_TEST(a_hit_returns_immediately);
    RUN_TEST(the_dismount_mask_skips_volumes);
    RUN_TEST(only_a_negative_high_word_validates_the_uid);
    RUN_TEST(diskless_skips_the_whole_walk);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
