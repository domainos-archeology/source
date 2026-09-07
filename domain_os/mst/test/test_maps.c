/*
 * mst/test/test_maps.c - Unit tests for MST_$MAPS (0x00E43982)
 *
 * Bead source-qmdl.  Exercises the real function (#included at the bottom)
 * with a mocked mst_$alloc_segs and checks the forwarding the 82-byte body
 * performs:
 *
 *   - the constant address hint 0x7FFFFFFF (`move.l #0x7fffffff,-(SP)`,
 *     0x00E439BE) is mst_$alloc_segs' first argument;
 *   - the caller's ten arguments land in the order the pushes at
 *     0x00E43990..0x00E439BA give them;
 *   - the global MST_$TOUCH_COUNT (0x00E439A0) is passed as touch_count;
 *   - argument 2 is a BYTE (`move.b (0xa,A6),-(SP)`, 0x00E43998), so the
 *     Pascal booleans true (0xFF) and false (0) reach mst_$alloc_segs'
 *     direction argument as themselves.  Passing the word 0xFF00 - the
 *     spelling this call took before the parameter was retyped - would have
 *     put a zero byte in that slot, which the test pins down explicitly;
 *   - argument 8 (`move.b (0x1e,A6),-(SP)`, 0x00E4399C) is a BYTE too;
 *   - the A0 result of mst_$alloc_segs is returned unchanged (0x00E439C8).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_mocks(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mst/mst_internal.h"

/* Captured mst_$alloc_segs arguments. */
static int      mock_alloc_calls;
static uint32_t mock_addr_hint;
static uid_t   *mock_uid;
static uint32_t mock_start_va;
static uint32_t mock_length;
static uint32_t mock_area_size;
static int16_t  mock_asid;
static uint16_t mock_area_id;
static uint16_t mock_touch_count;
static uint8_t  mock_access_rights;
static boolean  mock_direction;
static void    *mock_map_info;
static status_$t *mock_status;

static void *mock_alloc_result;

void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va,
                      uint32_t length, uint32_t area_size, int16_t asid,
                      uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, boolean direction,
                      void *map_info, status_$t *status)
{
    mock_alloc_calls++;
    mock_addr_hint = addr_hint;
    mock_uid = uid;
    mock_start_va = start_va;
    mock_length = length;
    mock_area_size = area_size;
    mock_asid = asid;
    mock_area_id = area_id;
    mock_touch_count = touch_count;
    mock_access_rights = access_rights;
    mock_direction = direction;
    mock_map_info = map_info;
    mock_status = status;
    return mock_alloc_result;
}

/* The globals mst_data.c would supply; MST_$MAPS only reads the touch count. */
uint16_t MST_$TOUCH_COUNT = 0;

static uid_t     test_uid;
static uint32_t  test_map_info;
static status_$t test_status;

static void reset_mocks(void)
{
    mock_alloc_calls = 0;
    mock_addr_hint = 0;
    mock_uid = NULL;
    mock_start_va = 0;
    mock_length = 0;
    mock_area_size = 0;
    mock_asid = 0;
    mock_area_id = 0;
    mock_touch_count = 0;
    mock_access_rights = 0;
    mock_direction = 0;
    mock_map_info = NULL;
    mock_status = NULL;
    mock_alloc_result = (void *)(uintptr_t)0x12345678u;
    MST_$TOUCH_COUNT = 4;               /* what MST_$INIT sets (0x00E30B90) */
    memset(&test_uid, 0, sizeof(test_uid));
    test_map_info = 0;
    test_status = 0;
}

/* The ten arguments arrive in the order the prologue pushes them. */
static void test_forwards_every_argument(void)
{
    void *result = MST_$MAPS(9, true, &test_uid, 0x1000, 0x10000, 0x16,
                             0x400, true, &test_map_info, &test_status);

    ASSERT_EQ(1, mock_alloc_calls);
    ASSERT_EQ(0x7FFFFFFFu, mock_addr_hint);      /* 0x00E439BE */
    ASSERT_EQ((uintptr_t)&test_uid, (uintptr_t)mock_uid);       /* 0x00E439BA */
    ASSERT_EQ(0x1000u, mock_start_va);           /* 0x00E439B6 */
    ASSERT_EQ(0x10000u, mock_length);            /* 0x00E439B2 */
    ASSERT_EQ(0x400u, mock_area_size);           /* 0x00E439AE */
    ASSERT_EQ(9, mock_asid);                     /* 0x00E439AA */
    ASSERT_EQ(0x16, mock_area_id);               /* 0x00E439A6 */
    ASSERT_EQ(4, mock_touch_count);              /* 0x00E439A0 */
    ASSERT_EQ((uintptr_t)&test_map_info, (uintptr_t)mock_map_info);
    ASSERT_EQ((uintptr_t)&test_status, (uintptr_t)mock_status);
    ASSERT_EQ(0x12345678u, (uintptr_t)result);   /* 0x00E439C8: the A0 result */
}

/*
 * Argument 2 is the BYTE at A6+0x0A.  A caller's `st -(SP)` puts 0xFF there
 * and the callee reads exactly that byte, so true must arrive as 0xFF.
 */
static void test_byte_argument_2_is_honoured(void)
{
    (void)MST_$MAPS(0, true, &test_uid, 0, 0x400, 0x16, 0, false,
                    &test_map_info, &test_status);

    ASSERT_EQ(1, mock_alloc_calls);
    ASSERT_EQ(0xFF, (uint8_t)mock_direction);      /* 0x00E43998 */
    ASSERT_EQ(0x00, (uint8_t)mock_access_rights);  /* 0x00E4399C */

    reset_mocks();
    (void)MST_$MAPS(0, false, &test_uid, 0, 0x400, 0x16, 0, true,
                    &test_map_info, &test_status);

    ASSERT_EQ(0x00, (uint8_t)mock_direction);
    ASSERT_EQ(0xFF, (uint8_t)mock_access_rights);
}

/*
 * The old int16_t spelling of argument 2: a caller that passed the word
 * 0xFF00 was describing the word slot, not the byte the callee reads.  Once
 * the parameter is a byte, that word truncates to 0 - i.e. false - which is
 * why every call site had to be respelled.
 */
static void test_word_spelling_would_lose_the_byte(void)
{
    (void)MST_$MAPS(0, (boolean)0xFF00, &test_uid, 0, 0x400, 0x16, 0, true,
                    &test_map_info, &test_status);

    ASSERT_EQ(0x00, (uint8_t)mock_direction);
    /* while the byte spelling keeps it */
    reset_mocks();
    (void)MST_$MAPS(0, (boolean)0xFF, &test_uid, 0, 0x400, 0x16, 0, true,
                    &test_map_info, &test_status);
    ASSERT_EQ(0xFF, (uint8_t)mock_direction);
}

/* MST_$TOUCH_COUNT is read at call time, not captured anywhere. */
static void test_touch_count_is_read_from_the_global(void)
{
    MST_$TOUCH_COUNT = 1;
    (void)MST_$MAPS(0, true, &test_uid, 0, 0x400, 0x16, 0, true,
                    &test_map_info, &test_status);
    ASSERT_EQ(1, mock_touch_count);

    MST_$TOUCH_COUNT = 0x1234;
    (void)MST_$MAPS(0, true, &test_uid, 0, 0x400, 0x16, 0, true,
                    &test_map_info, &test_status);
    ASSERT_EQ(0x1234, mock_touch_count);
}

int main(void)
{
    printf("Running MST_$MAPS tests...\n\n");

    RUN_TEST(forwards_every_argument);
    RUN_TEST(byte_argument_2_is_honoured);
    RUN_TEST(word_spelling_would_lose_the_byte);
    RUN_TEST(touch_count_is_read_from_the_global);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

/* The real implementation under test. */
#include "../maps.c"
