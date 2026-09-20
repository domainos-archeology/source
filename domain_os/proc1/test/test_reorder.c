/*
 * Tests for proc1_$reorder_if_needed (0x00E207D8) against the real
 * ready-list code: proc1/remove_from_ready_list.c,
 * proc1/insert_into_ready_list.c and proc1/reorder_if_needed.c are
 * included directly.  The list is a circular doubly-linked ring whose
 * sentinel's nextp is PROC1_$READY_PCB (the sentinel holds state 0x08,
 * below every real process).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1_internal.h"

int __host_intr_disable_count = 0;

proc1_t *PROC1_$READY_PCB;
uint16_t PROC1_$READY_COUNT;

#include "../remove_from_ready_list.c"
#include "../insert_into_ready_list.c"
#include "../reorder_if_needed.c"

static int tests_run, tests_failed;
#define ASSERT_EQ(a, b) do {                                                  \
    unsigned long long _a = (unsigned long long)(a);                          \
    unsigned long long _b = (unsigned long long)(b);                          \
    if (_a != _b) {                                                           \
        printf("  FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", __FILE__,       \
               __LINE__, #a, #b, _a, _b);                                     \
        tests_failed++;                                                       \
    }                                                                         \
} while (0)
#define ASSERT_PTR_EQ(a, b) ASSERT_EQ((uintptr_t)(a), (uintptr_t)(b))
#define RUN_TEST(fn) do { tests_run++; fn(); } while (0)

static proc1_t sentinel;

/*
 * Build the ring sentinel -> pcbs[0] -> pcbs[1] ... -> sentinel.  The
 * sentinel's nextp is mirrored into PROC1_$READY_PCB after every step,
 * as the shared cell in the image would be.
 */
static void build(proc1_t **pcbs, int n)
{
    int i;
    memset(&sentinel, 0, sizeof(sentinel));
    sentinel.state = 0x08;
    sentinel.nextp = sentinel.prevp = &sentinel;
    for (i = 0; i < n; i++) {
        proc1_t *p = pcbs[i];
        p->nextp = &sentinel;
        p->prevp = sentinel.prevp;
        sentinel.prevp->nextp = p;
        sentinel.prevp = p;
    }
    PROC1_$READY_PCB = sentinel.nextp;
    PROC1_$READY_COUNT = (uint16_t)n;
}

static void sync_head(void) { PROC1_$READY_PCB = sentinel.nextp; }

static void set(proc1_t *p, uint32_t locks, uint16_t state)
{
    p->resource_locks_held = locks;
    p->state = state;
}

static int ring_is(proc1_t **want, int n)
{
    proc1_t *p = sentinel.nextp;
    int i;
    for (i = 0; i < n; i++) {
        if (p != want[i]) return 0;
        if (p->nextp->prevp != p) return 0;
        p = p->nextp;
    }
    return p == &sentinel;
}

/* 0x00E207FE..0x00E20810: neither neighbour outranks - nothing moves */
static void test_no_move(void)
{
    proc1_t a, b, c;
    proc1_t *order[3] = { &a, &b, &c };
    set(&a, 0, 0x12); set(&b, 0, 0x10); set(&c, 0, 0x0E);
    build(order, 3);
    proc1_$reorder_if_needed(&b);
    ASSERT_EQ(ring_is(order, 3), 1);
    ASSERT_EQ(PROC1_$READY_COUNT, 3);
}

/* 0x00E207E2..0x00E207FC: prev has strictly less priority - move up (LIFO) */
static void test_move_ahead_of_prev(void)
{
    proc1_t a, b, c;
    proc1_t *order[3] = { &a, &b, &c };
    proc1_t *want[3] = { &b, &a, &c };
    set(&a, 0, 0x10); set(&b, 0, 0x10); set(&c, 0, 0x0E);
    build(order, 3);
    b.state = 0x11;                          /* b now outranks a */
    proc1_$reorder_if_needed(&b);
    sync_head();
    ASSERT_EQ(ring_is(want, 3), 1);
    ASSERT_EQ(PROC1_$READY_COUNT, 3);
}

/* equal locks and equal state with prev: `bls' keeps it in place */
static void test_equal_to_prev_stays(void)
{
    proc1_t a, b, c;
    proc1_t *order[3] = { &a, &b, &c };
    set(&a, 1, 0x10); set(&b, 1, 0x10); set(&c, 0, 0x10);
    build(order, 3);
    proc1_$reorder_if_needed(&b);
    ASSERT_EQ(ring_is(order, 3), 1);
}

/*
 * 0x00E20812..0x00E2081E: next outranks - remove, then resume the LIFO
 * walk at the former next.  With c and d equal to each other and above
 * b's new priority, b lands after d (the walk passes both), and ahead of
 * e, which it equals (LIFO: before equals).
 */
static void test_move_back_resumes_at_next(void)
{
    proc1_t a, b, c, d, e;
    proc1_t *order[5] = { &a, &b, &c, &d, &e };
    proc1_t *want[5] = { &a, &c, &d, &b, &e };
    set(&a, 0, 0x14); set(&b, 0, 0x13); set(&c, 0, 0x12);
    set(&d, 0, 0x12); set(&e, 0, 0x10);
    build(order, 5);
    b.state = 0x10;                          /* b dropped below c and d */
    proc1_$reorder_if_needed(&b);
    sync_head();
    ASSERT_EQ(ring_is(want, 5), 1);
    ASSERT_EQ(PROC1_$READY_COUNT, 5);
}

/* the head: 0x00E207DC skips the prev test even though prev (the
 * sentinel) ranks below it; next outranks it -> moves back */
static void test_head_moves_back(void)
{
    proc1_t a, b, c;
    proc1_t *order[3] = { &a, &b, &c };
    proc1_t *want[3] = { &b, &a, &c };
    set(&a, 2, 0x10); set(&b, 1, 0x10); set(&c, 0, 0x10);
    build(order, 3);
    a.resource_locks_held = 0;               /* now below b, equal to c */
    proc1_$reorder_if_needed(&a);
    sync_head();
    ASSERT_EQ(ring_is(want, 3), 1);
}

/* more locks than next: `bhi' at 0x00E20804 returns at once */
static void test_more_locks_than_next_stays(void)
{
    proc1_t a, b;
    proc1_t *order[2] = { &a, &b };
    set(&a, 4, 0x08); set(&b, 1, 0x10);
    build(order, 2);
    proc1_$reorder_if_needed(&a);
    ASSERT_EQ(ring_is(order, 2), 1);
}

int main(void)
{
    RUN_TEST(test_no_move);
    RUN_TEST(test_move_ahead_of_prev);
    RUN_TEST(test_equal_to_prev_stays);
    RUN_TEST(test_move_back_resumes_at_next);
    RUN_TEST(test_head_moves_back);
    RUN_TEST(test_more_locks_than_next_stays);
    printf("test_reorder: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
