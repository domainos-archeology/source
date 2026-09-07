/*
 * Tests for DXM_$ADD_CALLBACK (0x00E16FE0).
 *
 * dxm/add_callback.c is #included below and driven through mocked
 * ML_$SPIN_LOCK / ML_$SPIN_UNLOCK / EC_$ADVANCE_WITHOUT_DISPATCH /
 * CRASH_SYSTEM / CRASH_SHOW_STRING over a real dxm_queue_t.
 *
 * These cover the defect filed as source-w0bp: the 4th and 5th Pascal
 * parameters are a WORD data_size at (0x14,A6) (0x00E16FF6
 * `move.w (0x14,A6),D4w`) and a Domain boolean BYTE check_dup at (0x16,A6)
 * (0x00E16FFA `move.b (0x16,A6),D2b`), not one packed longword.  Every call
 * site cleans up exactly 20 bytes:
 *
 *   0x00E16F8A (TIME_$Q_SCAN_QUEUE)  lea (0x14,SP),SP
 *   0x00E1CE14 (KBD_$RCV)            lea (0x14,SP),SP
 *   0x00E17062 ... and the rest let `unlk` do it
 *
 * plus the constant cells the function reaches with `pea (d16,PC)`:
 *   0x00E17164  the status longword 0x00170002 (datum too large)
 *   0x00E17154  the crash-console string "(DXM) No room%"
 *
 * LAYOUT (source-wy9y): the image entry is 16 bytes and both the scan and
 * the insert scale their index by 16 (`lsl.l #0x4,D0` at 0x00E17060,
 * `lsl.w #0x4,D1w` at 0x00E17102).  dxm_entry_t now models `callback` as a
 * 4-byte dxm_$callback_t cell rather than a native function pointer, so the
 * record is 16 bytes on the host too and the entry array below is a plain
 * dxm_entry_t[].  DXM_$ADD_CALLBACK only ever compares and stores the cell
 * value (`cmpa.l (A3),A1` / `move.l (A3),(A0)`), never calls through it, so
 * the tests use literal code addresses for the two callbacks.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>

#include "base/base.h"
#include "dxm/dxm_internal.h"

/* ------------------------------------------------------------------ */
/* Globals the module under test refers to                             */
/* ------------------------------------------------------------------ */

uint32_t DXM_$OVERRUNS;
dxm_queue_t DXM_$UNWIRED_Q;
dxm_queue_t DXM_$WIRED_Q;

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

#define MOCK_TOKEN ((ml_$spin_token_t)0x5A5A)

static int n_lock, n_unlock, n_advance, n_crash, n_show_string;
static void *last_lockp;
static ml_$spin_token_t last_token;
static void *last_ec;
static status_$t last_crash_status;
static const char *last_crash_string;

static jmp_buf crash_jmp;
static int crash_longjmps;   /* when set, CRASH_SYSTEM does not return */

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    n_lock++;
    last_lockp = lockp;
    return MOCK_TOKEN;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    n_unlock++;
    last_lockp = lockp;
    last_token = token;
}

void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec)
{
    n_advance++;
    last_ec = ec;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    n_crash++;
    last_crash_status = *status_p;
    if (crash_longjmps) {
        longjmp(crash_jmp, 1);
    }
    /* The real CRASH_SYSTEM can return to its caller (0x00E1E7B0). */
}

void CRASH_SHOW_STRING(const char *str)
{
    n_show_string++;
    last_crash_string = str;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "dxm/add_callback.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

#define QSLOTS 8

static dxm_queue_t q;

static dxm_entry_t slot_mem[QSLOTS];

static dxm_entry_t *slot(int i)
{
    return &slot_mem[i];
}

/*
 * Two distinct callback cells.  The values stand in for code addresses in
 * the image's text; nothing in DXM_$ADD_CALLBACK dereferences them.
 */
#define CB_A ((dxm_$callback_t)0x00E1B8ACu)
#define CB_B ((dxm_$callback_t)0x00E72472u)   /* TERM_$ENQUEUE_TPAD */

static const dxm_$callback_t ptr_cb_a = CB_A;
static const dxm_$callback_t ptr_cb_b = CB_B;

static void reset(void)
{
    memset(&q, 0, sizeof q);
    memset(slot_mem, 0, sizeof slot_mem);
    q.head = 0;
    q.tail = 0;
    q.mask = QSLOTS - 1;
    q.entries = slot_mem;

    n_lock = n_unlock = n_advance = n_crash = n_show_string = 0;
    last_lockp = NULL;
    last_token = 0;
    last_ec = NULL;
    last_crash_status = 0;
    last_crash_string = NULL;
    crash_longjmps = 0;
    DXM_$OVERRUNS = 0;
}

/*
 * The callers pass a pointer to a cell holding the callback address and a
 * pointer to a cell holding the address of the payload.
 */
static void add(const dxm_$callback_t *cb, void *payload, uint16_t size,
                boolean dup, status_$t *st)
{
    void *data_cell = payload;
    DXM_$ADD_CALLBACK(&q, cb, &data_cell, size, dup, st);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * A plain insert.  data_size and check_dup are independent arguments, so
 * a 12-byte payload with dedup enabled -- exactly what TTY_$I_SIGNAL pushes
 * at 0x00E1B872/0x00E1B874 (`st -(SP)` then `move.w #0xc,-(SP)`) -- copies
 * all twelve bytes rather than the 0xFF00 the old packed encoding implied.
 */
static void test_basic_insert(void)
{
    static const uint8_t payload[12] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC
    };
    status_$t st = 0x7F7F7F7F;

    reset();
    add(&ptr_cb_a, (void *)payload, 12, true, &st);

    assert(st == status_$ok);            /* 0x00E17002 clr.l (A0) */
    assert(n_crash == 0);
    assert(q.tail == 1);                 /* 0x00E1712A */
    assert(q.head == 0);
    assert(slot(0)->callback == CB_A);   /* 0x00E17110 */
    assert(memcmp(slot(0)->data, payload, 12) == 0);
    assert(n_lock == 1 && n_unlock == 1);
    assert(last_token == MOCK_TOKEN);
    assert(n_advance == 1);              /* 0x00E17144 */
    assert(last_ec == &q.ec);            /* pea (0xc,A2) */
    assert(DXM_$OVERRUNS == 0);

    printf("test_basic_insert: PASSED\n");
}

/*
 * data_size 0 copies nothing and still inserts (num_words = (0+3)>>2 = 0,
 * `tst.w D5w` / `beq` at 0x00E17112).
 */
static void test_zero_size_insert(void)
{
    status_$t st = 0;

    reset();
    slot(0)->data[0] = 0xEE;
    add(&ptr_cb_a, NULL, 0, false, &st);

    assert(st == status_$ok);
    assert(q.tail == 1);
    assert(slot(0)->callback == CB_A);
    assert(slot(0)->data[0] == 0xEE);    /* untouched */
    assert(n_advance == 1);

    printf("test_zero_size_insert: PASSED\n");
}

/*
 * check_dup true (a Domain boolean, 0xFF) makes the scan at 0x00E17050 run
 * and a matching callback+data suppress the insert: the tail does not move
 * and the eventcount is NOT advanced (the `goto` at 0x00E170A6 lands on the
 * bare unlock at 0x00E170E8, which does not reach 0x00E17140).
 */
static void test_check_dup_true_suppresses(void)
{
    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    status_$t st = 0;

    reset();
    add(&ptr_cb_a, (void *)payload, 4, true, &st);
    assert(q.tail == 1);
    assert(n_advance == 1);

    add(&ptr_cb_a, (void *)payload, 4, true, &st);

    assert(st == status_$ok);
    assert(q.tail == 1);                 /* still one entry */
    assert(n_advance == 1);              /* no second advance */
    assert(n_lock == 2 && n_unlock == 2);

    printf("test_check_dup_true_suppresses: PASSED\n");
}

/*
 * check_dup false (0) skips the scan entirely (`tst.b D2b` / `bpl` at
 * 0x00E1704C), so an identical callback+data pair is queued twice.
 *
 * This is the case the old packed-longword signature got wrong in both
 * directions: 0x0CFF00 decoded to check_dup = 0x0C (positive, so no scan)
 * with data_size = 0xFF00 (which would have crashed).
 */
static void test_check_dup_false_does_not_scan(void)
{
    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    status_$t st = 0;

    reset();
    add(&ptr_cb_a, (void *)payload, 4, false, &st);
    add(&ptr_cb_a, (void *)payload, 4, false, &st);

    assert(q.tail == 2);
    assert(slot(0)->callback == CB_A);
    assert(slot(1)->callback == CB_A);
    assert(n_advance == 2);

    printf("test_check_dup_false_does_not_scan: PASSED\n");
}

/*
 * A different callback, or the same callback with different data, is not a
 * duplicate (0x00E17068 `cmpa.l (A3),A1`, then the byte loop at
 * 0x00E17088).
 */
static void test_dup_mismatches_insert(void)
{
    static const uint8_t p1[4] = { 1, 2, 3, 4 };
    static const uint8_t p2[4] = { 1, 2, 3, 5 };
    status_$t st = 0;

    reset();
    add(&ptr_cb_a, (void *)p1, 4, true, &st);
    assert(q.tail == 1);

    /* same data, different callback */
    add(&ptr_cb_b, (void *)p1, 4, true, &st);
    assert(q.tail == 2);

    /* same callback, last byte differs */
    add(&ptr_cb_a, (void *)p2, 4, true, &st);
    assert(q.tail == 3);

    assert(n_advance == 3);

    printf("test_dup_mismatches_insert: PASSED\n");
}

/*
 * The copy is by longwords -- num_words = (data_size + 3) >> 2 at
 * 0x00E1701A/0x00E1701C -- but the duplicate comparison is by data_size
 * BYTES (0x00E1707E `move.w D4w,D0w`).  With data_size 6 the eighth byte
 * is copied into the entry yet plays no part in the match.
 */
static void test_partial_word_size(void)
{
    static const uint8_t p1[8] = { 1, 2, 3, 4, 5, 6, 0xAA, 0xBB };
    static const uint8_t p2[8] = { 1, 2, 3, 4, 5, 6, 0xCC, 0xDD };
    status_$t st = 0;

    reset();
    add(&ptr_cb_a, (void *)p1, 6, true, &st);
    assert(q.tail == 1);
    /* two longwords were copied, so all eight bytes landed in the entry */
    assert(memcmp(slot(0)->data, p1, 8) == 0);

    /* bytes 6 and 7 differ but only 6 bytes are compared -> duplicate */
    add(&ptr_cb_a, (void *)p2, 6, true, &st);
    assert(q.tail == 1);
    assert(n_advance == 1);

    printf("test_partial_word_size: PASSED\n");
}

/*
 * data_size > 12 crashes (0x00E17004 `cmpi.w #0xc,D4w` / `bls`) with the
 * status constant at 0x00E17164, 0x00170002 = "datum too large for deferred
 * execution".  The real CRASH_SYSTEM can return, so the original would then
 * copy past its 12-byte frame local; the mock longjmps out instead of
 * reproducing the smash.
 */
static void test_datum_too_large_crashes(void)
{
    static uint8_t payload[64];
    status_$t st = 0;

    reset();
    crash_longjmps = 1;

    if (setjmp(crash_jmp) == 0) {
        add(&ptr_cb_a, payload, 13, false, &st);
        assert(!"CRASH_SYSTEM should have been called");
    }

    assert(n_crash == 1);
    assert(last_crash_status == 0x00170002);
    assert(n_lock == 0);                 /* the check precedes the lock */
    assert(q.tail == 0);

    printf("test_datum_too_large_crashes: PASSED\n");
}

/*
 * Exactly 12 bytes is allowed (`bls` is unsigned <=).
 */
static void test_max_size_allowed(void)
{
    static const uint8_t payload[12] = {
        0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5,
        0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB
    };
    status_$t st = 0;

    reset();
    add(&ptr_cb_a, (void *)payload, 12, false, &st);

    assert(n_crash == 0);
    assert(q.tail == 1);
    assert(memcmp(slot(0)->data, payload, 12) == 0);

    printf("test_max_size_allowed: PASSED\n");
}

/*
 * A full queue (0x00E170BE `cmp.w (A2),D0w`) sets
 * status_$dxm_no_more_deferred_execution_queue_slots, shows the string at
 * 0x00E17154, calls CRASH_SYSTEM with that same status, bumps
 * DXM_$OVERRUNS (0x00E170E4) and unlocks without advancing the eventcount.
 */
static void test_queue_full(void)
{
    static const uint8_t payload[4] = { 9, 9, 9, 9 };
    status_$t st = 0;
    int i;

    reset();

    /* QSLOTS-1 inserts fill the ring: tail+1 == head is "full" */
    for (i = 0; i < QSLOTS - 1; i++) {
        add(&ptr_cb_a, (void *)payload, 4, false, &st);
    }
    assert(q.tail == QSLOTS - 1);
    assert(n_advance == QSLOTS - 1);
    assert(n_crash == 0);

    add(&ptr_cb_a, (void *)payload, 4, false, &st);

    assert(st == status_$dxm_no_more_deferred_execution_queue_slots);
    assert(st == 0x00170001);
    assert(n_show_string == 1);
    assert(strcmp(last_crash_string, "(DXM) No room%") == 0);
    assert(n_crash == 1);
    assert(last_crash_status == 0x00170001);
    assert(DXM_$OVERRUNS == 1);
    assert(q.tail == QSLOTS - 1);        /* unchanged */
    assert(n_advance == QSLOTS - 1);     /* no advance on the full path */
    assert(n_lock == QSLOTS && n_unlock == QSLOTS);

    printf("test_queue_full: PASSED\n");
}

/*
 * The scan starts at head, not at 0, and wraps with the mask
 * (0x00E170A8 `addq.w #0x1,D3w` / `and.w (0x4,A2),D3w`).  With head past
 * the slot holding a matching entry, that entry is invisible to the scan.
 */
static void test_scan_starts_at_head(void)
{
    static const uint8_t payload[4] = { 7, 7, 7, 7 };
    status_$t st = 0;

    reset();
    add(&ptr_cb_a, (void *)payload, 4, true, &st);
    assert(q.tail == 1);

    /* consume the entry the way DXM_$SCAN_QUEUE would */
    q.head = 1;

    add(&ptr_cb_a, (void *)payload, 4, true, &st);

    assert(q.tail == 2);                 /* not treated as a duplicate */
    assert(n_advance == 2);

    printf("test_scan_starts_at_head: PASSED\n");
}

int main(void)
{
    printf("Running DXM_$ADD_CALLBACK tests...\n\n");

    test_basic_insert();
    test_zero_size_insert();
    test_check_dup_true_suppresses();
    test_check_dup_false_does_not_scan();
    test_dup_mismatches_insert();
    test_partial_word_size();
    test_datum_too_large_crashes();
    test_max_size_allowed();
    test_queue_full();
    test_scan_starts_at_head();

    printf("\nAll tests PASSED!\n");
    return 0;
}
