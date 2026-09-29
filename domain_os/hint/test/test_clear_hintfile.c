/*
 * hint/test/test_clear_hintfile.c - HINT_$clear_hintfile (0x00E31194).
 *
 * Bead source-nrfl.  The outer loop is
 *
 *   00e311e0  moveq   #0x40,D0
 *   00e311e2  move.l  A2,D5           ; D5 = the bucket cursor
 *   ...
 *   00e31218  addi.l  #0x54,D5
 *   00e3121e  dbf     D0w,0x00e311e4
 *
 * i.e. 0x41 = 65 iterations at a 0x54 stride, so it clears bucket slots
 * 0..64.  HINT_$add_internal reduces its key with `andi.w #0x3f,D0w`
 * (0x00E49A5E) and therefore only ever selects 0..63.  hint_file_t has to be
 * 65 buckets wide for the last iteration to stay inside the record; it was
 * declared 64 wide, which made the image's own clear an out-of-bounds write.
 *
 * The inner two loops are checked too: three slots per bucket at a 0x1C
 * stride (0x00E311E6 `moveq #0x2,D1`) and, inside each, the key longword
 * plus three 8-byte address pairs (0x00E311F4 `moveq #0x2,D3`).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %-46s ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals and mocks
 * ========================================================================== */

#include "hint/hint_internal.h"

MODULE_DATA_DEFINE(hint_globals_t, HINT_$DATA, 0x00E7DB50);
hint_file_t   *HINT_$HINTFILE_PTR;
uint32_t       NODE_$ME;
uint32_t       ROUTE_$PORT;
route_$port_t  ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);

static int      truncate_calls;
static uid_t   *truncate_uid;
static uint32_t truncate_size;
static uint16_t truncate_flags;
static boolean *truncate_result_cell;

void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   boolean *result, status_$t *status)
{
    truncate_calls++;
    truncate_uid = uid;
    truncate_size = new_size;
    truncate_flags = flags;
    truncate_result_cell = result;
    *result = 0;
    *status = status_$ok;
}

/*
 * A hint file with one guard bucket past the record, so an overrun of the
 * clear loop shows up as a modified guard rather than as silent corruption.
 */
static struct {
    hint_file_t   file;
    hint_bucket_t guard;
} image;

#include "../clear_hintfile.c"

/* ==========================================================================
 * Tests
 * ========================================================================== */

static void fill(void)
{
    memset(&image, 0xA5, sizeof(image));
    HINT_$HINTFILE_PTR = &image.file;
    ROUTE_$WIRED_DATA.portp[0] = &ROUTE_$PORT_ARRAY[0];
    ROUTE_$PORT_ARRAY[0].active    = 0x1111;   /* +0x2C */
    ROUTE_$PORT_ARRAY[0].port_type = 0x2222;   /* +0x2E */
    ROUTE_$PORT_ARRAY[0].socket    = 0x3333;   /* +0x30 */
    truncate_calls = 0;
}

/* 0x00E3119C-0x00E311BA */
TEST(truncates_the_file_to_zero_first)
{
    fill();
    HINT_$clear_hintfile();

    ASSERT_EQ(1, truncate_calls);
    ASSERT_EQ((uintptr_t)&HINT_$HINTFILE_UID, (uintptr_t)truncate_uid);
    ASSERT_EQ(0u, truncate_size);       /* 0x00E311A8 clr.l */
    ASSERT_EQ(0u, truncate_flags);      /* 0x00E311A6 clr.w */
}

/* 0x00E311C4-0x00E311DE: the three header longwords. */
TEST(header_is_version_seven_zero_and_the_port_pair)
{
    fill();
    HINT_$clear_hintfile();

    ASSERT_EQ(HINT_FILE_VERSION, image.file.header.version);
    ASSERT_EQ(7u, image.file.header.version);
    ASSERT_EQ(0u, image.file.header.net_port);
    /*
     * 0x00E311D8 `move.l (0x2e,A2),(0x8,A0)` - ONE longword from
     * ROUTE_$PORTP[0]+0x2E, i.e. port_type in the high half and socket in
     * the low half.
     */
    ASSERT_EQ(0x22223333u, image.file.header.net_info);
}

/*
 * source-nrfl: bucket slots 0..64 are all cleared, and nothing past the
 * record is touched.
 */
TEST(clears_sixty_five_buckets_and_no_more)
{
    int b, s, a;
    const uint8_t *guard = (const uint8_t *)&image.guard;
    size_t i;

    fill();
    HINT_$clear_hintfile();

    for (b = 0; b < HINT_HASH_SLOTS; b++) {
        for (s = 0; s < HINT_SLOTS_PER_BUCKET; s++) {
            ASSERT_EQ(0u, image.file.buckets[b].slots[s].uid_low_masked);
            for (a = 0; a < HINT_ADDRS_PER_SLOT; a++) {
                ASSERT_EQ(0u, image.file.buckets[b].slots[s].addrs[a].flags);
                ASSERT_EQ(0u, image.file.buckets[b].slots[s].addrs[a].node_id);
            }
        }
    }

    /* bucket 64 is the one a 64-wide record would have missed */
    ASSERT_EQ(0u, image.file.buckets[HINT_HASH_SIZE].slots[0].uid_low_masked);

    for (i = 0; i < sizeof(image.guard); i++) {
        ASSERT_EQ(0xA5, guard[i]);
    }
}

/* The clear covers every byte from 0x0C to the end of the record. */
TEST(clear_extent_matches_the_stride_arithmetic)
{
    const uint8_t *p = (const uint8_t *)&image.file;
    size_t i;

    fill();
    HINT_$clear_hintfile();

    for (i = 0x0C; i < sizeof(hint_file_t); i++) {
        ASSERT_EQ(0, p[i]);
    }
    ASSERT_EQ(0x0C + 65 * 0x54, sizeof(hint_file_t));
}

int main(void)
{
    printf("HINT_$clear_hintfile (0x00E31194) tests\n");
    RUN_TEST(truncates_the_file_to_zero_first);
    RUN_TEST(header_is_version_seven_zero_and_the_port_pair);
    RUN_TEST(clears_sixty_five_buckets_and_no_more);
    RUN_TEST(clear_extent_matches_the_stride_arithmetic);
    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
