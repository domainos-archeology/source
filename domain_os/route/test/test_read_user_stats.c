/*
 * route/test/test_read_user_stats.c - Unit tests for ROUTE_$READ_USER_STATS
 * (0x00E6A65E).
 *
 * The point of interest is the argument frame: five slots, the third a word
 * at A6+0x10 that the routine never reads.  The tests call it with five
 * arguments and check that the reserved one has no effect, then cover the
 * not-found arm, the ten fixed bytes, the signed bucket count and the length
 * arithmetic.
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
        printf("  Running %s... ", #name);                                    \
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
 * Globals and mocks the code under test links against
 * ========================================================================== */

#include "route/route_internal.h"

route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];

static int find_port_calls;
static int16_t find_port_result;
static uint16_t find_port_net_seen;
static int32_t find_port_sock_seen;

int16_t ROUTE_$FIND_PORT(uint16_t network, int32_t socket)
{
    find_port_calls++;
    find_port_net_seen = network;
    find_port_sock_seen = socket;
    return find_port_result;
}

static route_$port_stats_t test_stats;

#include "../read_user_stats.c"

/* ==========================================================================
 * Harness
 * ========================================================================== */

static uint8_t buf[0x100];
static int16_t length_ret;
static status_$t status_ret;
static uint16_t socket_word;

static void reset_state(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)&test_stats - 0x1000;

    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    memset(&test_stats, 0, sizeof(test_stats));
    memset(buf, 0xEE, sizeof(buf));

    find_port_calls = 0;
    find_port_result = 1;
    length_ret = -1;
    status_ret = 0x5A5A5A5A;
    socket_word = 0x1234;

    ROUTE_$PORT_ARRAY[1].driver_stats = ARCH_PTR_TO_VA(&test_stats);
}

/* Set the +0x34 longword (its low half is the +0x36 word). */
static void set_bucket_count(int idx, uint32_t count)
{
    ROUTE_$PORT_ARRAY[idx].queue_len_hi = (uint16_t)(count >> 16);
    ROUTE_$PORT_ARRAY[idx].socket2 = (uint16_t)count;
}

/* ==========================================================================
 * The argument frame
 * ========================================================================== */

/*
 * The frame the prologue reads: (0x8) socket_ptr, (0xc) stats_buf, a WORD at
 * (0x10) that is never referenced, (0x12) length_ret, (0x16) status_ret.
 * Passing wildly different values for the reserved word must change nothing.
 */
TEST(reserved_word_is_never_read)
{
    uint8_t buf_a[0x100];
    uint8_t buf_b[0x100];
    int16_t len_a, len_b;
    status_$t st_a, st_b;

    reset_state();
    test_stats.flags = 0xAB00;
    test_stats.deep_queue_puts = 0x11223344;
    test_stats.failed_puts = 0x55667788;
    set_bucket_count(1, 2);
    memset(buf_a, 0xEE, sizeof(buf_a));
    ROUTE_$READ_USER_STATS(&socket_word, buf_a, 0x0000, &len_a, &st_a);

    reset_state();
    test_stats.flags = 0xAB00;
    test_stats.deep_queue_puts = 0x11223344;
    test_stats.failed_puts = 0x55667788;
    set_bucket_count(1, 2);
    memset(buf_b, 0xEE, sizeof(buf_b));
    ROUTE_$READ_USER_STATS(&socket_word, buf_b, 0xFFFF, &len_b, &st_b);

    ASSERT_EQ(0, memcmp(buf_a, buf_b, sizeof(buf_a)));
    ASSERT_EQ(len_a, len_b);
    ASSERT_EQ(st_a, st_b);
}

/*
 * 0x00E6A670-0x00E6A67E: the socket word is ZERO-extended ("clr.l D0 /
 * move.w (A0),D0w") and the network is the constant 2.
 */
TEST(find_port_arguments)
{
    reset_state();
    socket_word = 0xFFFE;
    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);

    ASSERT_EQ(1, find_port_calls);
    ASSERT_EQ(ROUTE_PORT_TYPE_ROUTING, find_port_net_seen);
    /* zero-extended, not sign-extended */
    ASSERT_EQ(0x0000FFFE, find_port_sock_seen);
}

/* 0x00E6A686-0x00E6A698: the not-found arm clears the length as a WORD. */
TEST(unknown_port)
{
    reset_state();
    find_port_result = -1;
    length_ret = 0x7777;

    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);

    ASSERT_EQ(0, length_ret);
    ASSERT_EQ(status_$internet_unknown_network_port, status_ret);
    /* Nothing was copied. */
    ASSERT_EQ(0xEE, buf[0]);
}

/* ==========================================================================
 * The copy
 * ========================================================================== */

/*
 * 0x00E6A6B4-0x00E6A6C0: one byte at +0x00 then two longwords at +0x02 and
 * +0x06.  Byte 1 of the caller's buffer is left alone.
 */
TEST(fixed_head_copy)
{
    route_$port_stats_t *out = (route_$port_stats_t *)buf;

    reset_state();
    test_stats.flags = 0xAB00;               /* byte 0 is 0xAB on m68k */
    test_stats.deep_queue_puts = 0x11223344;
    test_stats.failed_puts = 0x55667788;
    set_bucket_count(1, 0);

    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);

    ASSERT_EQ(0xAB, buf[0]);
    /* the second byte of the flags word is not copied */
    ASSERT_EQ(0xEE, buf[1]);
    ASSERT_EQ(0x11223344, out->deep_queue_puts);
    ASSERT_EQ(0x55667788, out->failed_puts);
    ASSERT_EQ(status_$ok, status_ret);
}

/*
 * 0x00E6A6C4-0x00E6A6DC: "dbf D0w" with D0 = the +0x36 word copies count+1
 * buckets, in index order, and stops there.
 */
TEST(bucket_copy_count)
{
    route_$port_stats_t *out = (route_$port_stats_t *)buf;
    int i;

    reset_state();
    for (i = 0; i < 0x21; i++) {
        test_stats.queue_depth[i] = 0x1000 + (uint32_t)i;
    }
    set_bucket_count(1, 2);

    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);

    /* count = 2 -> three buckets, 0..2 */
    ASSERT_EQ(0x1000, out->queue_depth[0]);
    ASSERT_EQ(0x1001, out->queue_depth[1]);
    ASSERT_EQ(0x1002, out->queue_depth[2]);
    /* bucket 3 was never written */
    ASSERT_EQ(0xEEEEEEEE, out->queue_depth[3]);

    /* count = 0 still copies one bucket. */
    reset_state();
    for (i = 0; i < 0x21; i++) {
        test_stats.queue_depth[i] = 0x2000 + (uint32_t)i;
    }
    set_bucket_count(1, 0);
    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);
    ASSERT_EQ(0x2000, out->queue_depth[0]);
    ASSERT_EQ(0xEEEEEEEE, out->queue_depth[1]);
}

/* 0x00E6A6C8 "bmi": a negative word copies no buckets at all. */
TEST(negative_bucket_count_copies_nothing)
{
    route_$port_stats_t *out = (route_$port_stats_t *)buf;

    reset_state();
    test_stats.queue_depth[0] = 0x3000;
    set_bucket_count(1, 0xFFFFFFFFu);        /* +0x36 reads as -1 */

    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);

    ASSERT_EQ(0xEEEEEEEE, out->queue_depth[0]);
    /* The fixed head was still copied, and the length still computed. */
    ASSERT_EQ(status_$ok, status_ret);
    /* (0xFFFFFFFF + 1) * 4 + 10 == 10 */
    ASSERT_EQ(10, length_ret);
}

/*
 * 0x00E6A6DE-0x00E6A6F2: the length is STATS_BASE_SIZE + (count + 1) * 4,
 * with the count taken from the LONGWORD at +0x34.
 */
TEST(length_arithmetic)
{
    reset_state();
    set_bucket_count(1, 0);
    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);
    ASSERT_EQ(10 + 4, length_ret);

    reset_state();
    set_bucket_count(1, 2);
    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);
    ASSERT_EQ(10 + 12, length_ret);

    reset_state();
    set_bucket_count(1, 0x20);
    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);
    ASSERT_EQ(10 + 0x21 * 4, length_ret);
    ASSERT_EQ(sizeof(route_$port_stats_t), length_ret);
}

/*
 * The length uses the whole longword at +0x34 while the bucket copy uses only
 * its low half, so a non-zero high half moves them apart.  Reproduced as
 * found - the image reads two different widths of the same cell.
 */
TEST(length_uses_the_longword_the_copy_uses_the_word)
{
    route_$port_stats_t *out = (route_$port_stats_t *)buf;

    reset_state();
    test_stats.queue_depth[0] = 0x4000;
    test_stats.queue_depth[1] = 0x4001;
    ROUTE_$PORT_ARRAY[1].queue_len_hi = 1;    /* longword = 0x00010001 */
    ROUTE_$PORT_ARRAY[1].socket2 = 1;

    ROUTE_$READ_USER_STATS(&socket_word, buf, 0, &length_ret, &status_ret);

    /* the word says 1, so two buckets */
    ASSERT_EQ(0x4000, out->queue_depth[0]);
    ASSERT_EQ(0x4001, out->queue_depth[1]);
    ASSERT_EQ(0xEEEEEEEE, out->queue_depth[2]);

    /* the longword says 0x10001, so the length is the low word of
     * 10 + 0x10002*4 = 0xA + 0x40008 = 0x40012 -> 0x0012 */
    ASSERT_EQ((int16_t)0x0012, length_ret);
}

int main(void)
{
    printf("ROUTE_$READ_USER_STATS tests\n");

    RUN_TEST(reserved_word_is_never_read);
    RUN_TEST(find_port_arguments);
    RUN_TEST(unknown_port);
    RUN_TEST(fixed_head_copy);
    RUN_TEST(bucket_copy_count);
    RUN_TEST(negative_bucket_count_copies_nothing);
    RUN_TEST(length_arithmetic);
    RUN_TEST(length_uses_the_longword_the_copy_uses_the_word);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
