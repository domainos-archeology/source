/*
 * disk/test/test_add_que.c - Unit tests for DISK_$ADD_QUE (0x00E3C716)
 *
 * disk/add_que.c is #included below and driven through a host copy of the
 * disk module data area.  Request blocks and the queue live in an arena
 * that ARCH_HOST_VA_BASE points at; the VA cells hold arena offsets
 * (counted from 1, since VA 0 is nil).  ML_$LOCK / ML_$UNLOCK /
 * ML_$SPIN_LOCK / ML_$SPIN_UNLOCK / CRASH_SYSTEM are mocked.
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "disk/disk_internal.h"
#include "misc/misc.h"
#include "ml/ml.h"
#include "arch/arch.h"

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

/* ============================================================================
 * Module data and mocks
 * ============================================================================ */

uint8_t DISK_$DATA[DISK_$DATA_SIZE];

static int lock_calls;
static int unlock_calls;
static int16_t last_lock_id;
static int spin_locks;
static int spin_unlocks;
static uint32_t spin_lock_new_position;   /* if nonzero, stored under the lock */
static ml_$spin_token_t last_token;
static jmp_buf crash_jmp;
static status_$t crash_status;

void ML_$LOCK(int16_t id)   { lock_calls++; last_lock_id = id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; last_lock_id = id; }

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    spin_locks++;
    if (spin_lock_new_position != 0) {
        ((disk_$que_t *)lockp)->position = spin_lock_new_position;
    }
    return 0x2700;
}

void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot)
{
    ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token;
    (void)lockp;
    spin_unlocks++;
    last_token = token;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_status = *status_p;
    longjmp(crash_jmp, 1);
}

/* ============================================================================
 * Arena
 * ============================================================================ */

#define REQ_SLOT(i)     ((uint32_t)(0x100 + (i) * 0x40))   /* VA of request i */
#define QUEUE_VA        0x40u

static uint8_t arena[0x1000];

#include "../add_que.c"

static disk_io_req_t *req(int i)
{
    return (disk_io_req_t *)(arena + REQ_SLOT(i));
}

static disk_$que_t *que(void)
{
    return (disk_$que_t *)(arena + QUEUE_VA);
}

static uint32_t va(const void *p)
{
    return ARCH_PTR_TO_VA(p);
}

/* dev record: word +0x08 flags, word +0x0a lock id */
static uint8_t dev[0x20];

static void set_dev(uint16_t flags, int16_t lock_id)
{
    memset(dev, 0, sizeof dev);
    /* both words are read through aligned uint16_t loads */
    *(uint16_t *)(dev + 0x08) = flags;
    *(uint16_t *)(dev + 0x0a) = (uint16_t)lock_id;
}

/* Rebuild the queue exactly as DISK_$INIT_QUE leaves it. */
static void reset(uint32_t position)
{
    disk_$que_t *q;

    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    memset(arena, 0, sizeof arena);
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    q = que();
    q->current = 0;
    q->position = position;
    q->list_a = va(&q->sentinel_a);
    q->list_b = va(&q->sentinel_b);
    q->sentinel_a.next = 0;
    q->sentinel_a.daddr = DISK_QUE_SENTINEL_CYL;
    q->sentinel_b.next = 0;
    q->sentinel_b.daddr = DISK_QUE_SENTINEL_CYL;
    lock_calls = unlock_calls = spin_locks = spin_unlocks = 0;
    spin_lock_new_position = 0;
    set_dev(0, 0x11);
}

/* Make request i: cylinder `cyl`, absolute address `lba`, transfer chunk
 * `chunk` in the +0x1c word, chained after request `prev` (0 = head). */
static void mk(int i, uint16_t cyl, uint32_t lba, uint16_t chunk, int prev)
{
    disk_io_req_t *r = req(i);
    memset(r, 0, sizeof *r);
    r->daddr = ((uint32_t)cyl << 16) | 0x0001;
    r->header[7] = lba;
    r->flags = chunk;
    if (prev != 0) {
        req(prev)->next = va(r);
    }
}

static uint32_t sentinel_a_va(void) { return va(&que()->sentinel_a); }
static uint32_t sentinel_b_va(void) { return va(&que()->sentinel_b); }

#define POS(cyl) ((uint32_t)(cyl) << DISK_QUE_POSITION_SHIFT)
#define UP       DISK_QUE_DIRECTION_BIT

/* ============================================================================
 * Tests
 * ============================================================================ */

/* Three unsorted single-block requests, disk at cylinder 0 heading up:
 * everything is ahead of the head and lands on list_a in ascending order. */
TEST(sorts_and_lists_ahead_ascending)
{
    reset(UP | POS(0));
    mk(1, 20, 2000, 1, 0);
    mk(2, 5,  500,  1, 1);
    mk(3, 10, 1000, 1, 2);

    DISK_$ADD_QUE(0, dev, que(), req(1));

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0, unlock_calls);
    ASSERT_EQ(0x11, last_lock_id);
    ASSERT_EQ(1, spin_locks);
    ASSERT_EQ(1, spin_unlocks);
    ASSERT_EQ(0x2700, last_token);

    /* run array: 0, r2, r3, r1, 0 */
    ASSERT_EQ(0, DISK_$QUE_ARRAY[0]);
    ASSERT_EQ(REQ_SLOT(2), DISK_$QUE_ARRAY[1]);
    ASSERT_EQ(REQ_SLOT(3), DISK_$QUE_ARRAY[2]);
    ASSERT_EQ(REQ_SLOT(1), DISK_$QUE_ARRAY[3]);
    ASSERT_EQ(0, DISK_$QUE_ARRAY[4]);

    /* list_a: r2 -> r3 -> r1 -> sentinel_a */
    ASSERT_EQ(REQ_SLOT(2), que()->list_a);
    ASSERT_EQ(REQ_SLOT(3), req(2)->next);
    ASSERT_EQ(REQ_SLOT(1), req(3)->next);
    ASSERT_EQ(sentinel_a_va(), req(1)->next);
    ASSERT_EQ(sentinel_b_va(), que()->list_b);

    /* each run is one request: end = self, count = 1 */
    ASSERT_EQ(REQ_SLOT(1), req(1)->reserved_18);
    ASSERT_EQ(REQ_SLOT(2), req(2)->reserved_18);
    ASSERT_EQ(REQ_SLOT(3), req(3)->reserved_18);
    ASSERT_EQ(1, req(1)->flags);
    ASSERT_EQ(1, req(2)->flags);
    ASSERT_EQ(1, req(3)->flags);

    /* the empty-list splice marks the parent's group_end (0x00E3C618) */
    ASSERT_EQ(0x40, req(1)->op_flags);
    ASSERT_EQ(0, req(2)->op_flags);
    ASSERT_EQ(0, req(3)->op_flags);
}

/* Two contiguous requests on one cylinder (chunk 4, addresses 100 and 104)
 * plus a third on the same cylinder that is not contiguous. */
TEST(groups_contiguous_blocks)
{
    reset(UP | POS(0));
    mk(1, 7, 100, 4, 0);
    mk(2, 7, 104, 4, 1);
    mk(3, 7, 200, 4, 2);

    DISK_$ADD_QUE(1, dev, que(), req(1));

    /* one cylinder run */
    ASSERT_EQ(REQ_SLOT(1), DISK_$QUE_ARRAY[1]);
    ASSERT_EQ(0, DISK_$QUE_ARRAY[2]);

    /* group r1..r2 (count 2) - the group end lands on the SECOND request
     * of the group (0x00E3C870-0x00E3C872) */
    ASSERT_EQ(2, req(1)->flags);
    ASSERT_EQ(REQ_SLOT(2), req(2)->reserved_18);
    /* new group r3 (count 1) */
    ASSERT_EQ(1, req(3)->flags);
    /* the run head records the run's last request */
    ASSERT_EQ(REQ_SLOT(3), req(1)->reserved_18);

    /* list_a: r1 -> r2 -> r3 -> sentinel_a */
    ASSERT_EQ(REQ_SLOT(1), que()->list_a);
    ASSERT_EQ(REQ_SLOT(2), req(1)->next);
    ASSERT_EQ(REQ_SLOT(3), req(2)->next);
    ASSERT_EQ(sentinel_a_va(), req(3)->next);
    ASSERT_EQ(0x40, req(3)->op_flags);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0, unlock_calls);
}

/* Disk at cylinder 12 heading down: runs 5 and 10 are ahead (list_a,
 * descending), run 20 is behind (list_b, ascending). */
TEST(heading_down_splits_around_head)
{
    reset(POS(12));
    mk(1, 5,  500,  1, 0);
    mk(2, 10, 1000, 1, 1);
    mk(3, 20, 2000, 1, 2);

    DISK_$ADD_QUE(1, dev, que(), req(1));

    /* list_b: r3 -> sentinel_b, marked */
    ASSERT_EQ(REQ_SLOT(3), que()->list_b);
    ASSERT_EQ(sentinel_b_va(), req(3)->next);
    ASSERT_EQ(0x40, req(3)->op_flags);

    /* list_a: r2 -> r1 -> sentinel_a, not marked (count != 0) */
    ASSERT_EQ(REQ_SLOT(2), que()->list_a);
    ASSERT_EQ(REQ_SLOT(1), req(2)->next);
    ASSERT_EQ(sentinel_a_va(), req(1)->next);
    ASSERT_EQ(0, req(1)->op_flags);
    ASSERT_EQ(0, req(2)->op_flags);
}

/* Disk at cylinder 12 heading up: runs 5 and 10 are behind (list_b,
 * descending), run 20 ahead (list_a). */
TEST(heading_up_splits_around_head)
{
    reset(UP | POS(12));
    mk(1, 5,  500,  1, 0);
    mk(2, 10, 1000, 1, 1);
    mk(3, 20, 2000, 1, 2);

    DISK_$ADD_QUE(1, dev, que(), req(1));

    /* list_b: r2 -> r1 -> sentinel_b, last spliced run end marked */
    ASSERT_EQ(REQ_SLOT(2), que()->list_b);
    ASSERT_EQ(REQ_SLOT(1), req(2)->next);
    ASSERT_EQ(sentinel_b_va(), req(1)->next);
    ASSERT_EQ(0x40, req(1)->op_flags);
    ASSERT_EQ(0, req(2)->op_flags);

    /* list_a was empty: r3 -> sentinel_a, group_end not marked */
    ASSERT_EQ(REQ_SLOT(3), que()->list_a);
    ASSERT_EQ(sentinel_a_va(), req(3)->next);
    ASSERT_EQ(0, req(3)->op_flags);
}

/* Merging into a non-empty ascending list: an existing run at cylinder 10
 * on list_a, new runs at 5 and 10 and 15.  The new run at 10 takes over the
 * existing run's end pointer (0x00E3C65E). */
TEST(merges_into_existing_ascending_list)
{
    reset(UP | POS(0));
    /* existing: r9 (cyl 10) -> sentinel_a, run end = itself */
    mk(9, 10, 900, 1, 0);
    req(9)->reserved_18 = REQ_SLOT(9);
    req(9)->next = sentinel_a_va();
    que()->list_a = REQ_SLOT(9);

    mk(1, 5,  500,  1, 0);
    mk(2, 10, 1000, 1, 1);
    mk(3, 15, 1500, 1, 2);

    DISK_$ADD_QUE(1, dev, que(), req(1));

    /* list_a: r1 -> r2 -> r9 -> r3 -> sentinel_a */
    ASSERT_EQ(REQ_SLOT(1), que()->list_a);
    ASSERT_EQ(REQ_SLOT(2), req(1)->next);
    ASSERT_EQ(REQ_SLOT(9), req(2)->next);
    ASSERT_EQ(REQ_SLOT(3), req(9)->next);
    ASSERT_EQ(sentinel_a_va(), req(3)->next);
    /* r2's run end became r9's run end before r2 was linked */
    ASSERT_EQ(REQ_SLOT(9), req(2)->reserved_18);
    ASSERT_EQ(0x40, req(3)->op_flags);
}

/* The "already sorted" flag with an out-of-order chain: the run loop
 * notices, releases the lock and sorts after all (0x00E3C80C-0x00E3C830). */
TEST(presorted_flag_falls_back_to_sort)
{
    reset(UP | POS(0));
    mk(1, 7, 200, 1, 0);
    mk(2, 7, 100, 1, 1);

    DISK_$ADD_QUE(1, dev, que(), req(1));

    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    /* one run, r2 first */
    ASSERT_EQ(REQ_SLOT(2), DISK_$QUE_ARRAY[1]);
    ASSERT_EQ(REQ_SLOT(2), que()->list_a);
    ASSERT_EQ(REQ_SLOT(1), req(2)->next);
    ASSERT_EQ(sentinel_a_va(), req(1)->next);
    ASSERT_EQ(REQ_SLOT(1), req(2)->reserved_18);
}

/* The head moves between the unlocked position read and the spin lock:
 * the boundary is searched again under the lock (0x00E3C8F4-0x00E3C926). */
TEST(position_change_under_lock_is_rescanned)
{
    reset(UP | POS(0));
    mk(1, 5,  500,  1, 0);
    mk(2, 10, 1000, 1, 1);
    mk(3, 20, 2000, 1, 2);
    spin_lock_new_position = UP | POS(12);

    DISK_$ADD_QUE(1, dev, que(), req(1));

    /* same outcome as heading_up_splits_around_head */
    ASSERT_EQ(REQ_SLOT(2), que()->list_b);
    ASSERT_EQ(REQ_SLOT(1), req(2)->next);
    ASSERT_EQ(sentinel_b_va(), req(1)->next);
    ASSERT_EQ(0x40, req(1)->op_flags);
    ASSERT_EQ(REQ_SLOT(3), que()->list_a);
    ASSERT_EQ(sentinel_a_va(), req(3)->next);
    ASSERT_EQ(0, req(3)->op_flags);
}

/* Heading down with the head moving under the lock: the descending search
 * from the top of the array (0x00E3C974-0x00E3C99E). */
TEST(position_change_under_lock_heading_down)
{
    reset(POS(0));
    mk(1, 5,  500,  1, 0);
    mk(2, 10, 1000, 1, 1);
    mk(3, 20, 2000, 1, 2);
    spin_lock_new_position = POS(12);

    DISK_$ADD_QUE(1, dev, que(), req(1));

    ASSERT_EQ(REQ_SLOT(3), que()->list_b);
    ASSERT_EQ(sentinel_b_va(), req(3)->next);
    ASSERT_EQ(0x40, req(3)->op_flags);
    ASSERT_EQ(REQ_SLOT(2), que()->list_a);
    ASSERT_EQ(REQ_SLOT(1), req(2)->next);
    ASSERT_EQ(sentinel_a_va(), req(1)->next);
}

/* Heading down with every run below the head: below == count, so nothing
 * goes to list_b and list_a's splice is marked. */
TEST(heading_down_all_below)
{
    reset(POS(30));
    mk(1, 5,  500,  1, 0);
    mk(2, 10, 1000, 1, 1);

    DISK_$ADD_QUE(1, dev, que(), req(1));

    ASSERT_EQ(sentinel_b_va(), que()->list_b);
    ASSERT_EQ(REQ_SLOT(2), que()->list_a);
    ASSERT_EQ(REQ_SLOT(1), req(2)->next);
    ASSERT_EQ(sentinel_a_va(), req(1)->next);
    ASSERT_EQ(0x40, req(1)->op_flags);
}

TEST(queued_driver_crashes)
{
    reset(UP | POS(0));
    mk(1, 5, 500, 1, 0);
    set_dev(0x0200, 0x11);
    crash_status = 0;

    if (setjmp(crash_jmp) == 0) {
        DISK_$ADD_QUE(0, dev, que(), req(1));
        ASSERT_EQ(1, 0);    /* must not return */
    }
    ASSERT_EQ(0x0008002E, crash_status);
    ASSERT_EQ(0, lock_calls);
}

int main(void)
{
    printf("test_add_que:\n");
    RUN_TEST(sorts_and_lists_ahead_ascending);
    RUN_TEST(groups_contiguous_blocks);
    RUN_TEST(heading_down_splits_around_head);
    RUN_TEST(heading_up_splits_around_head);
    RUN_TEST(merges_into_existing_ascending_list);
    RUN_TEST(presorted_flag_falls_back_to_sort);
    RUN_TEST(position_change_under_lock_is_rescanned);
    RUN_TEST(position_change_under_lock_heading_down);
    RUN_TEST(heading_down_all_below);
    RUN_TEST(queued_driver_crashes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
