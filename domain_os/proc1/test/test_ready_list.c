/*
 * Test cases for proc1 ready list insertion functions
 *
 * Tests the two insertion strategies:
 *   - proc1_$add_ready_body: FIFO within same priority (round-robin)
 *   - proc1_$insert_into_ready_list: LIFO within same priority
 *
 * Both functions order by:
 *   1. resource_locks_held (descending - more locks = higher priority)
 *   2. state (ascending - lower state = higher priority)
 *
 * The ready list is a circular doubly-linked list with a sentinel node.
 * In the kernel, PROC1_$READY_PCB is physically the sentinel's nextp
 * field (they share the same memory location). This means scanning
 * always starts from the first real PCB, and when list operations
 * modify sentinel's nextp, PROC1_$READY_PCB automatically updates.
 *
 * The sentinel has state lower than any real process (0x08 in the kernel),
 * so the insertion loop always terminates before walking past it.
 *
 * Also tests proc1_$remove_from_ready_list and PROC1_$ADD_READY.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Minimal PCB structure for testing - matches offsets from proc1.h
 */
typedef struct proc1_t {
    struct proc1_t *nextp;          /* 0x00 */
    struct proc1_t *prevp;          /* 0x04 */
    uint32_t    pad_08[14];         /* 0x08-0x3F: saved regs, etc. */
    uint32_t    resource_locks_held;/* 0x40 */
    uint16_t    pad_44[7];          /* 0x44-0x51 */
    uint16_t    state;              /* 0x52 */
    uint8_t     pad_54[14];         /* 0x54-0x61 */
} proc1_t;

/*
 * Sentinel node. In the kernel, the sentinel's nextp field IS
 * PROC1_$READY_PCB (same memory address). We simulate this with
 * a macro so that list operations on sentinel.nextp automatically
 * update what PROC1_$READY_PCB sees.
 *
 * Sentinel state = 0x08 matches the kernel. All real processes
 * must have state > 0x08.
 */
#define SENTINEL_STATE 0x08
static proc1_t sentinel;

/*
 * PROC1_$READY_PCB is an alias for sentinel.nextp, matching the
 * kernel memory layout where they occupy the same address.
 */
#define PROC1_$READY_PCB (sentinel.nextp)

uint16_t PROC1_$READY_COUNT;

/* ========================================================================
 * Re-implement functions for testing (avoids kernel header dependencies)
 * ======================================================================== */

static void proc1_$add_ready_body(proc1_t *pcb)
{
    proc1_t *pos;
    proc1_t *prev;
    uint32_t locks = pcb->resource_locks_held;
    uint16_t state = pcb->state;

    pos = PROC1_$READY_PCB;

    /* FIFO: skip past entries with equal priority (bls in assembly) */
    while (locks <= pos->resource_locks_held) {
        if (locks != pos->resource_locks_held) {
            pos = pos->nextp;
            continue;
        }
        if (state > pos->state) {
            break;
        }
        pos = pos->nextp;
    }

    pcb->nextp = pos;
    prev = pos->prevp;
    pcb->prevp = prev;
    pos->prevp = pcb;
    prev->nextp = pcb;

    PROC1_$READY_COUNT++;
}

static void proc1_$insert_into_ready_list(proc1_t *pcb)
{
    proc1_t *pos;
    proc1_t *prev;
    uint32_t locks = pcb->resource_locks_held;
    uint16_t state = pcb->state;

    pos = PROC1_$READY_PCB;

    /* LIFO: stop at entries with equal priority (bcs in assembly) */
    while (locks <= pos->resource_locks_held) {
        if (locks != pos->resource_locks_held) {
            pos = pos->nextp;
            continue;
        }
        if (state >= pos->state) {
            break;
        }
        pos = pos->nextp;
    }

    pcb->nextp = pos;
    prev = pos->prevp;
    pcb->prevp = prev;
    pos->prevp = pcb;
    prev->nextp = pcb;

    PROC1_$READY_COUNT++;
}

static void proc1_$remove_from_ready_list(proc1_t *pcb)
{
    proc1_t *next = pcb->nextp;
    proc1_t *prev = pcb->prevp;

    prev->nextp = next;
    next->prevp = prev;

    PROC1_$READY_COUNT--;
}

/* PROC1_$ADD_READY: public wrapper, uses FIFO semantics */
static void PROC1_$ADD_READY(proc1_t *pcb)
{
    proc1_$add_ready_body(pcb);
}

/* ========================================================================
 * Test infrastructure
 * ======================================================================== */

static void init_ready_list(void)
{
    memset(&sentinel, 0, sizeof(sentinel));
    sentinel.nextp = &sentinel;
    sentinel.prevp = &sentinel;
    sentinel.resource_locks_held = 0;
    sentinel.state = SENTINEL_STATE;
    PROC1_$READY_COUNT = 0;
}

static void init_pcb(proc1_t *pcb, uint32_t locks, uint16_t state)
{
    memset(pcb, 0, sizeof(*pcb));
    pcb->resource_locks_held = locks;
    pcb->state = state;
}

/*
 * Verify list integrity:
 *  - Forward and backward links are consistent
 *  - Count matches PROC1_$READY_COUNT
 */
static void verify_list_integrity(int expected_count)
{
    proc1_t *cur;
    int count = 0;

    cur = PROC1_$READY_PCB;
    do {
        assert(cur->nextp->prevp == cur);
        assert(cur->prevp->nextp == cur);
        if (cur != &sentinel) {
            count++;
        }
        cur = cur->nextp;
    } while (cur != PROC1_$READY_PCB);

    assert(count == expected_count);
    assert(PROC1_$READY_COUNT == (uint16_t)expected_count);
}

/*
 * Collect list order into an array for verification.
 * Returns the number of PCBs (excluding sentinel).
 */
static int collect_list(proc1_t **out, int max)
{
    proc1_t *cur = PROC1_$READY_PCB;
    int n = 0;

    do {
        if (cur != &sentinel && n < max) {
            out[n++] = cur;
        }
        cur = cur->nextp;
    } while (cur != PROC1_$READY_PCB);

    return n;
}

/* ========================================================================
 * Tests for proc1_$insert_into_ready_list (LIFO within same priority)
 * ======================================================================== */

static void test_insert_single(void)
{
    proc1_t a;

    init_ready_list();
    init_pcb(&a, 0, 0x10);

    proc1_$insert_into_ready_list(&a);

    assert(PROC1_$READY_COUNT == 1);
    verify_list_integrity(1);

    printf("test_insert_single: PASSED\n");
}

static void test_insert_by_locks(void)
{
    proc1_t a, b, c;
    proc1_t *order[3];

    init_ready_list();
    init_pcb(&a, 1, 0x10);  /* 1 lock */
    init_pcb(&b, 3, 0x10);  /* 3 locks - highest priority */
    init_pcb(&c, 2, 0x10);  /* 2 locks */

    proc1_$insert_into_ready_list(&a);
    proc1_$insert_into_ready_list(&b);
    proc1_$insert_into_ready_list(&c);

    verify_list_integrity(3);

    /* Order should be: b(3) -> c(2) -> a(1) */
    collect_list(order, 3);
    assert(order[0] == &b);
    assert(order[1] == &c);
    assert(order[2] == &a);

    printf("test_insert_by_locks: PASSED\n");
}

static void test_insert_by_state(void)
{
    proc1_t a, b, c;
    proc1_t *order[3];

    init_ready_list();
    init_pcb(&a, 0, 0x20);  /* state 32 - highest priority */
    init_pcb(&b, 0, 0x10);  /* state 16 - lowest priority */
    init_pcb(&c, 0, 0x18);  /* state 24 - middle */

    proc1_$insert_into_ready_list(&a);
    proc1_$insert_into_ready_list(&b);
    proc1_$insert_into_ready_list(&c);

    verify_list_integrity(3);

    /*
     * State ordering is DESCENDING: higher state = closer to head.
     * Order should be: a(32) -> c(24) -> b(16)
     */
    collect_list(order, 3);
    assert(order[0] == &a);
    assert(order[1] == &c);
    assert(order[2] == &b);

    printf("test_insert_by_state: PASSED\n");
}

static void test_insert_lifo_same_priority(void)
{
    proc1_t a, b, c;
    proc1_t *order[3];

    init_ready_list();
    init_pcb(&a, 0, 0x10);
    init_pcb(&b, 0, 0x10);
    init_pcb(&c, 0, 0x10);

    proc1_$insert_into_ready_list(&a);
    proc1_$insert_into_ready_list(&b);
    proc1_$insert_into_ready_list(&c);

    verify_list_integrity(3);

    /*
     * LIFO: each new insert goes BEFORE entries with equal priority.
     * Insert a: [a]
     * Insert b: [b, a]  (b inserted before a because state >= a.state)
     * Insert c: [c, b, a]  (c inserted before b)
     */
    collect_list(order, 3);
    assert(order[0] == &c);
    assert(order[1] == &b);
    assert(order[2] == &a);

    printf("test_insert_lifo_same_priority: PASSED\n");
}

/* ========================================================================
 * Tests for proc1_$add_ready_body (FIFO within same priority)
 * ======================================================================== */

static void test_add_ready_body_single(void)
{
    proc1_t a;

    init_ready_list();
    init_pcb(&a, 0, 0x10);

    proc1_$add_ready_body(&a);

    assert(PROC1_$READY_COUNT == 1);
    verify_list_integrity(1);

    printf("test_add_ready_body_single: PASSED\n");
}

static void test_add_ready_body_by_locks(void)
{
    proc1_t a, b, c;
    proc1_t *order[3];

    init_ready_list();
    init_pcb(&a, 1, 0x10);
    init_pcb(&b, 3, 0x10);
    init_pcb(&c, 2, 0x10);

    proc1_$add_ready_body(&a);
    proc1_$add_ready_body(&b);
    proc1_$add_ready_body(&c);

    verify_list_integrity(3);

    /* Same ordering as insert_into_ready_list for different lock counts */
    collect_list(order, 3);
    assert(order[0] == &b);
    assert(order[1] == &c);
    assert(order[2] == &a);

    printf("test_add_ready_body_by_locks: PASSED\n");
}

static void test_add_ready_body_fifo_same_priority(void)
{
    proc1_t a, b, c;
    proc1_t *order[3];

    init_ready_list();
    init_pcb(&a, 0, 0x10);
    init_pcb(&b, 0, 0x10);
    init_pcb(&c, 0, 0x10);

    proc1_$add_ready_body(&a);
    proc1_$add_ready_body(&b);
    proc1_$add_ready_body(&c);

    verify_list_integrity(3);

    /*
     * FIFO: each new insert goes AFTER entries with equal priority.
     * Insert a: [a]
     * Insert b: [a, b]  (b walks past a because state <= a.state)
     * Insert c: [a, b, c]  (c walks past a and b)
     */
    collect_list(order, 3);
    assert(order[0] == &a);
    assert(order[1] == &b);
    assert(order[2] == &c);

    printf("test_add_ready_body_fifo_same_priority: PASSED\n");
}

static void test_add_ready_body_mixed(void)
{
    proc1_t a, b, c, d, e;
    proc1_t *order[5];

    init_ready_list();
    init_pcb(&a, 2, 0x10);   /* 2 locks, state 16 */
    init_pcb(&b, 0, 0x18);   /* 0 locks, state 24 */
    init_pcb(&c, 2, 0x10);   /* same as a */
    init_pcb(&d, 0, 0x18);   /* same as b */
    init_pcb(&e, 1, 0x14);   /* 1 lock, state 20 */

    proc1_$add_ready_body(&a);
    proc1_$add_ready_body(&b);
    proc1_$add_ready_body(&c);
    proc1_$add_ready_body(&d);
    proc1_$add_ready_body(&e);

    verify_list_integrity(5);

    /*
     * Expected order (sorted by locks desc, then state asc, FIFO within same):
     * a(locks=2, state=16), c(locks=2, state=16),  -- FIFO: a before c
     * e(locks=1, state=20),
     * b(locks=0, state=24), d(locks=0, state=24)  -- FIFO: b before d
     */
    collect_list(order, 5);
    assert(order[0] == &a);
    assert(order[1] == &c);
    assert(order[2] == &e);
    assert(order[3] == &b);
    assert(order[4] == &d);

    printf("test_add_ready_body_mixed: PASSED\n");
}

/* ========================================================================
 * Tests for PROC1_$ADD_READY (public wrapper)
 * ======================================================================== */

static void test_add_ready_wrapper(void)
{
    proc1_t a, b;
    proc1_t *order[2];

    init_ready_list();
    init_pcb(&a, 0, 0x10);
    init_pcb(&b, 0, 0x10);

    PROC1_$ADD_READY(&a);
    PROC1_$ADD_READY(&b);

    verify_list_integrity(2);

    /* Should use FIFO semantics (same as add_ready_body) */
    collect_list(order, 2);
    assert(order[0] == &a);
    assert(order[1] == &b);

    printf("test_add_ready_wrapper: PASSED\n");
}

/* ========================================================================
 * Tests for proc1_$remove_from_ready_list
 * ======================================================================== */

static void test_remove_single(void)
{
    proc1_t a;

    init_ready_list();
    init_pcb(&a, 0, 0x10);

    proc1_$add_ready_body(&a);
    assert(PROC1_$READY_COUNT == 1);

    proc1_$remove_from_ready_list(&a);
    assert(PROC1_$READY_COUNT == 0);

    /* List should be just the sentinel */
    assert(sentinel.nextp == &sentinel);
    assert(sentinel.prevp == &sentinel);

    printf("test_remove_single: PASSED\n");
}

static void test_remove_middle(void)
{
    proc1_t a, b, c;
    proc1_t *order[2];

    init_ready_list();
    init_pcb(&a, 0, 0x20);  /* state 32 - highest priority */
    init_pcb(&b, 0, 0x18);  /* state 24 - middle */
    init_pcb(&c, 0, 0x10);  /* state 16 - lowest priority */

    proc1_$add_ready_body(&a);
    proc1_$add_ready_body(&b);
    proc1_$add_ready_body(&c);

    verify_list_integrity(3);

    /* List is: a(32) -> b(24) -> c(16) -> sentinel */
    proc1_$remove_from_ready_list(&b);

    verify_list_integrity(2);

    collect_list(order, 2);
    assert(order[0] == &a);
    assert(order[1] == &c);

    printf("test_remove_middle: PASSED\n");
}

/* ========================================================================
 * Tests contrasting FIFO vs LIFO behavior
 * ======================================================================== */

static void test_fifo_vs_lifo(void)
{
    proc1_t a1, b1, c1;
    proc1_t a2, b2, c2;
    proc1_t *order[3];

    /* Test FIFO (add_ready_body) */
    init_ready_list();
    init_pcb(&a1, 0, 0x10);
    init_pcb(&b1, 0, 0x10);
    init_pcb(&c1, 0, 0x10);

    proc1_$add_ready_body(&a1);
    proc1_$add_ready_body(&b1);
    proc1_$add_ready_body(&c1);

    collect_list(order, 3);
    /* FIFO: insertion order preserved */
    assert(order[0] == &a1);
    assert(order[1] == &b1);
    assert(order[2] == &c1);

    /* Test LIFO (insert_into_ready_list) */
    init_ready_list();
    init_pcb(&a2, 0, 0x10);
    init_pcb(&b2, 0, 0x10);
    init_pcb(&c2, 0, 0x10);

    proc1_$insert_into_ready_list(&a2);
    proc1_$insert_into_ready_list(&b2);
    proc1_$insert_into_ready_list(&c2);

    collect_list(order, 3);
    /* LIFO: insertion order reversed */
    assert(order[0] == &c2);
    assert(order[1] == &b2);
    assert(order[2] == &a2);

    printf("test_fifo_vs_lifo: PASSED\n");
}

int main(void)
{
    printf("Running proc1 ready list tests...\n\n");

    /* insert_into_ready_list tests */
    test_insert_single();
    test_insert_by_locks();
    test_insert_by_state();
    test_insert_lifo_same_priority();

    /* add_ready_body tests */
    test_add_ready_body_single();
    test_add_ready_body_by_locks();
    test_add_ready_body_fifo_same_priority();
    test_add_ready_body_mixed();

    /* Public wrapper test */
    test_add_ready_wrapper();

    /* Remove tests */
    test_remove_single();
    test_remove_middle();

    /* Comparative test */
    test_fifo_vs_lifo();

    printf("\nAll tests PASSED!\n");
    return 0;
}
