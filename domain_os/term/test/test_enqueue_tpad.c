/*
 * term/test/test_enqueue_tpad.c - Unit tests for TERM_$ENQUEUE_TPAD
 * (0x00E72472)
 *
 * TPAD_$DATA and M$OIS$WLW are stubs; the tests pin the double indirection
 * through the DXM datum, the 16-byte slot addressing (base + 4 + tail*16),
 * the modulo-6 wrap of the tail, that head is re-read on every pass, and
 * that an empty queue makes no call.
 */

#include <stdio.h>
#include <string.h>

#include "term/term_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ---------------------------------------------------------------- stubs */

static tpad_buffer_t queue;
static const void *data_log[16];
static int data_calls;
static int bump_head_on_call;     /* simulate a producer racing the drain */

void TPAD_$DATA(tpad_$data_packet_t *packet)
{
    if (data_calls < 16) {
        data_log[data_calls] = packet;
    }
    data_calls++;
    if (bump_head_on_call == data_calls) {
        queue.head = (uint16_t)((queue.head + 1) % SUMA_TPAD_BUFFER_SIZE);
    }
}

short M$OIS$WLW(long dividend, short divisor)
{
    return (short)(dividend % divisor);
}

#include "../enqueue_tpad.c"

/* ---------------------------------------------------------------- tests */

static tpad_buffer_t *cell;       /* the cell the DXM datum points at */
static void *datum;               /* the 4-byte DXM datum */

static void reset(void)
{
    memset(&queue, 0, sizeof(queue));
    memset(data_log, 0, sizeof(data_log));
    data_calls = 0;
    bump_head_on_call = 0;
    cell = &queue;
    datum = &cell;
}

TEST(empty_queue_no_calls)
{
    reset();
    queue.head = 3;
    queue.tail = 3;
    TERM_$ENQUEUE_TPAD(&datum);
    ASSERT_EQ(0, data_calls);
    ASSERT_EQ(3, queue.tail);
}

TEST(drains_two_entries_in_order)
{
    reset();
    queue.head = 4;
    queue.tail = 2;
    TERM_$ENQUEUE_TPAD(&datum);
    ASSERT_EQ(2, data_calls);
    ASSERT_EQ((unsigned long)&queue.samples[2], (unsigned long)data_log[0]);
    ASSERT_EQ((unsigned long)&queue.samples[3], (unsigned long)data_log[1]);
    ASSERT_EQ((unsigned long)((uint8_t *)&queue + 4 + 2 * 16), (unsigned long)data_log[0]);
    ASSERT_EQ(4, queue.tail);
}

TEST(tail_wraps_at_six)
{
    reset();
    queue.head = 1;
    queue.tail = 5;
    TERM_$ENQUEUE_TPAD(&datum);
    ASSERT_EQ(2, data_calls);
    ASSERT_EQ((unsigned long)&queue.samples[5], (unsigned long)data_log[0]);
    ASSERT_EQ((unsigned long)&queue.samples[0], (unsigned long)data_log[1]);
    ASSERT_EQ(1, queue.tail);
}

TEST(head_reread_each_pass)
{
    reset();
    queue.head = 1;
    queue.tail = 0;
    bump_head_on_call = 1;        /* producer adds one while the first drains */
    TERM_$ENQUEUE_TPAD(&datum);
    ASSERT_EQ(2, data_calls);
    ASSERT_EQ(2, queue.tail);
    ASSERT_EQ(2, queue.head);
}

int main(void)
{
    printf("TERM_$ENQUEUE_TPAD tests\n");
    RUN_TEST(empty_queue_no_calls);
    RUN_TEST(drains_two_entries_in_order);
    RUN_TEST(tail_wraps_at_six);
    RUN_TEST(head_reread_each_pass);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
