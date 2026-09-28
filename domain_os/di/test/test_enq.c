/*
 * Tests for DI_$ENQ (0x00E209A8) and DI_$INIT_Q_ELEM (0x00E209D6).
 * Pinned: the double-enqueue arm crashes with 0x000A000C and does not
 * queue; a fresh element is pushed at the head with both args and 0xFF.
 */
#include <stdio.h>
#include <string.h>
#include "base/base.h"
#include "di/di_internal.h"

int __host_intr_disable_count = 0;
static int n_crash; static const status_$t *last_crash;
void CRASH_SYSTEM(const status_$t *s) { n_crash++; last_crash = s; }

#include "di/enq.c"
#include "di/init_q_elem.c"

static int tests_run, tests_failed;
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { tests_run++; n_crash = 0; DI_$Q_HEAD = NULL; name(); } while (0)
#define ASSERT_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { printf("  FAIL %s:%d: %s == %lld, expected %s == %lld\n", __FILE__, __LINE__, #a, _a, #b, _b); \
    tests_failed++; return; } } while (0)

TEST(init_clears_16_bytes)
{
    di_queue_elem_t e; memset(&e, 0xEE, sizeof(e));
    DI_$INIT_Q_ELEM(&e);
    /* the image clears 16 bytes = the whole element; on a 64-bit host the
     * pointer alone is 8, so only the first three fields are covered */
    ASSERT_EQ(e.next == NULL, 1); ASSERT_EQ(e.arg1, 0); ASSERT_EQ(e.arg2, 0);
    if (sizeof(void *) == 4) { ASSERT_EQ(e.enqueued, 0); ASSERT_EQ(e.reserved[2], 0); }
}

TEST(enq_pushes_at_head)
{
    di_queue_elem_t a, b; DI_$INIT_Q_ELEM(&a); DI_$INIT_Q_ELEM(&b);
    DI_$ENQ(1, 2, &a);
    ASSERT_EQ(DI_$Q_HEAD == &a, 1); ASSERT_EQ(a.next == NULL, 1);
    ASSERT_EQ(a.arg1, 1); ASSERT_EQ(a.arg2, 2); ASSERT_EQ(a.enqueued, 0xFF);
    DI_$ENQ(3, 4, &b);
    ASSERT_EQ(DI_$Q_HEAD == &b, 1); ASSERT_EQ(b.next == &a, 1);
    ASSERT_EQ(n_crash, 0);
}

TEST(enq_twice_crashes_and_leaves_queue)
{
    di_queue_elem_t a; DI_$INIT_Q_ELEM(&a);
    DI_$ENQ(1, 2, &a);
    DI_$ENQ(9, 9, &a);
    ASSERT_EQ(n_crash, 1); ASSERT_EQ(*last_crash, status_$proc1_bad_deferred_interrupt_queue);
    ASSERT_EQ(a.arg1, 1); ASSERT_EQ(DI_$Q_HEAD == &a, 1); ASSERT_EQ(a.next == NULL, 1);
}

int main(void)
{
    RUN_TEST(init_clears_16_bytes); RUN_TEST(enq_pushes_at_head); RUN_TEST(enq_twice_crashes_and_leaves_queue);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
