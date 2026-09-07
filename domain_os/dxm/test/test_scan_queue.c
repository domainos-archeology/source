/*
 * Tests for DXM_$SCAN_QUEUE (0x00E17168) and the callback-cell accessor.
 *
 * dxm/dxm_data.c and dxm/scan_queue.c are #included below, so this test
 * drives the real DXM_$SCAN_QUEUE over a real dxm_queue_t and the real
 * host callback registry.
 *
 * Points covered (source-wy9y):
 *   - dxm_entry_t is 16 bytes on the host as well as on m68k, so the
 *     `lsl.w #4,D0w` / `lea (0,A0,D0w),A0` indexing at 0x00E17172-0x00E1717A
 *     lands on the same bytes DXM_$ADD_CALLBACK wrote;
 *   - the callback is stored as a 4-byte cell and dxm_$callback_fn() maps
 *     it back to something callable;
 *   - the head advances with the mask BEFORE the callback runs
 *     (0x00E1718C `addq.w #1,D0w` / `and.w (0x4,A2),D0w` / `move.w D0w,(A2)`,
 *     then the `jsr (A1)` at 0x00E171C0), and the queue lock is dropped
 *     across the call;
 *   - the callback receives the ADDRESS of a local holding &entry->data
 *     (`pea (-0xc,A6)` at 0x00E171BA), i.e. a `void **`.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "base/base.h"
#include "dxm/dxm_internal.h"

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

#define MOCK_TOKEN ((ml_$spin_token_t)0x3C3C)

static int n_lock, n_unlock;
static ml_$spin_token_t last_token;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    (void)lockp;
    n_lock++;
    return MOCK_TOKEN;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    (void)lockp;
    n_unlock++;
    last_token = token;
}

void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec) { (void)ec; }

/* Referenced by dxm/dxm_data.c's signal-routine table. */
void PROC2_$SIGNAL_OS(uid_t *uid, int16_t *signal, uint32_t *param,
                      status_$t *st)
{
    (void)uid; (void)signal; (void)param; (void)st;
}

void PROC2_$SIGNAL_PGROUP_OS(uid_t *uid, int16_t *signal, uint32_t *param,
                             status_$t *st)
{
    (void)uid; (void)signal; (void)param; (void)st;
}

/* Referenced by dxm/dxm_data.c's PTR_DXM_$ADD_SIGNAL_CALLBACK cell. */
void DXM_$ADD_SIGNAL_CALLBACK(void *data) { (void)data; }

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "dxm/dxm_data.c"
#include "dxm/scan_queue.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

#define QSLOTS 4

static dxm_queue_t q;
static dxm_entry_t slot_mem[QSLOTS];

/* Trace of what the callbacks saw. */
#define MAX_SEEN 8
static int n_seen;
static int seen_which[MAX_SEEN];
static uint32_t seen_first_word[MAX_SEEN];
static uint16_t seen_head[MAX_SEEN];
static int seen_lock_depth[MAX_SEEN];

static void record(int which, void *arg)
{
    void **data_cell = (void **)arg;
    const uint8_t *data = (const uint8_t *)*data_cell;

    if (n_seen < MAX_SEEN) {
        seen_which[n_seen] = which;
        seen_first_word[n_seen] = ((uint32_t)data[0] << 24) |
                                  ((uint32_t)data[1] << 16) |
                                  ((uint32_t)data[2] << 8) |
                                  (uint32_t)data[3];
        seen_head[n_seen] = q.head;
        seen_lock_depth[n_seen] = n_lock - n_unlock;
        n_seen++;
    }
}

static void cb_a(void *arg) { record(0, arg); }
static void cb_b(void *arg) { record(1, arg); }

static void reset(void)
{
    memset(&q, 0, sizeof q);
    memset(slot_mem, 0, sizeof slot_mem);
    q.head = 0;
    q.tail = 0;
    q.mask = QSLOTS - 1;
    q.entries = slot_mem;

    n_lock = n_unlock = 0;
    last_token = 0;
    n_seen = 0;
}

static void put(int idx, dxm_$callback_t cb, uint32_t first_word)
{
    slot_mem[idx].callback = cb;
    slot_mem[idx].data[0] = (uint8_t)(first_word >> 24);
    slot_mem[idx].data[1] = (uint8_t)(first_word >> 16);
    slot_mem[idx].data[2] = (uint8_t)(first_word >> 8);
    slot_mem[idx].data[3] = (uint8_t)first_word;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * The record is pointer-free and must be 16 bytes on every target; the
 * asserts in dxm/dxm.h are unconditional, this just states it in the test
 * output too.
 */
static void test_entry_is_16_bytes(void)
{
    assert(sizeof(dxm_entry_t) == 16);
    assert(DXM_ENTRY_SIZE == 16);
    assert((size_t)((uint8_t *)&slot_mem[1] - (uint8_t *)&slot_mem[0]) == 16);
    assert(offsetof(dxm_entry_t, data) == 4);

    printf("test_entry_is_16_bytes: PASSED\n");
}

/*
 * The host registry hands out distinct non-zero cells and maps them back.
 */
static void test_callback_registry(void)
{
    dxm_$callback_t ca = DXM_$CALLBACK_CELL(cb_a);
    dxm_$callback_t cb = DXM_$CALLBACK_CELL(cb_b);

    assert(ca != 0);
    assert(cb != 0);
    assert(ca != cb);
    /* registering the same function again returns the same cell */
    assert(DXM_$CALLBACK_CELL(cb_a) == ca);
    assert(dxm_$callback_fn(ca) == cb_a);
    assert(dxm_$callback_fn(cb) == cb_b);
    assert(dxm_$callback_fn(0) == NULL);

    printf("test_callback_registry: PASSED\n");
}

/*
 * An empty queue locks once, finds head == tail, unlocks and returns
 * without calling anything (0x00E171CA .. 0x00E171DC).
 */
static void test_empty_queue(void)
{
    reset();

    DXM_$SCAN_QUEUE(&q);

    assert(n_lock == 1);
    assert(n_unlock == 1);
    assert(last_token == MOCK_TOKEN);
    assert(n_seen == 0);
    assert(q.head == 0);

    printf("test_empty_queue: PASSED\n");
}

/*
 * Two queued entries run in head order.  Each callback sees the head
 * already advanced past its own entry and the queue lock released
 * (the unlock at 0x00E171AC precedes the `jsr (A1)` at 0x00E171C0).
 */
static void test_drains_in_order(void)
{
    reset();

    put(0, DXM_$CALLBACK_CELL(cb_a), 0xAABBCCDDu);
    put(1, DXM_$CALLBACK_CELL(cb_b), 0x01020304u);
    q.head = 0;
    q.tail = 2;

    DXM_$SCAN_QUEUE(&q);

    assert(n_seen == 2);
    assert(seen_which[0] == 0);
    assert(seen_first_word[0] == 0xAABBCCDDu);
    assert(seen_head[0] == 1);
    assert(seen_lock_depth[0] == 0);

    assert(seen_which[1] == 1);
    assert(seen_first_word[1] == 0x01020304u);
    assert(seen_head[1] == 2);
    assert(seen_lock_depth[1] == 0);

    assert(q.head == q.tail);
    /* one lock/unlock per entry plus the final empty check */
    assert(n_lock == 3 && n_unlock == 3);

    printf("test_drains_in_order: PASSED\n");
}

/*
 * The head wraps with the mask (`and.w (0x4,A2),D0w` at 0x00E17192), so a
 * queue whose head is at the last slot and whose tail has wrapped to 1
 * drains slot 3 then slot 0.
 */
static void test_head_wraps_with_mask(void)
{
    reset();

    put(3, DXM_$CALLBACK_CELL(cb_a), 0x33333333u);
    put(0, DXM_$CALLBACK_CELL(cb_b), 0x00000001u);
    q.head = 3;
    q.tail = 1;

    DXM_$SCAN_QUEUE(&q);

    assert(n_seen == 2);
    assert(seen_which[0] == 0);
    assert(seen_first_word[0] == 0x33333333u);
    assert(seen_head[0] == 0);          /* (3 + 1) & 3 */
    assert(seen_which[1] == 1);
    assert(seen_head[1] == 1);
    assert(q.head == 1);

    printf("test_head_wraps_with_mask: PASSED\n");
}

int main(void)
{
    printf("Running DXM_$SCAN_QUEUE tests...\n\n");

    test_entry_is_16_bytes();
    test_callback_registry();
    test_empty_queue();
    test_drains_in_order();
    test_head_wraps_with_mask();

    printf("\nAll tests PASSED!\n");
    return 0;
}
