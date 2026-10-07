/*
 * Tests for the ML_$ exclusion / resource lock functions.
 *
 * These tests #include the real .c files under test and drive the real
 * functions through mocked PROC1_$ / EC_$ / CRASH_SYSTEM dependencies.
 *
 * They specifically cover the defects found in the 2026-09-06 audit:
 *   - the deferred-suspend test in the shared release epilogue
 *     (0x00E20ED0 btst.b #2,(0x55,A1) == mask 0x04, not 0x400), which
 *     was previously unreachable;
 *   - the priority-boost re-insertion at 0x00E20EC0/0x00E20EC8;
 *   - the missing `ori #0x700,SR` on ML_$EXCLUSION_STOP's no-waiter
 *     path (0x00E20EAC);
 *   - the forced IPL 0 exits (0x00E20EEA / 0x00E20E9C);
 *   - `resource_locks_held &= ~1` at 0x00E20EB0.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "base/base.h"
#include "ml/ml.h"
#include "proc1/proc1.h"

/* ------------------------------------------------------------------ */
/* Host runtime / mocks                                                */
/* ------------------------------------------------------------------ */

int __host_intr_disable_count = 0;

static proc1_t mock_pcb;
proc1_t *PROC1_$CURRENT_PCB = &mock_pcb;
proc1_t proc1_$pcb_pool[PROC1_MAX_PROCESSES - 1];  /* PROC1_$READY_PCB = pool[1].nextp */


/* Call trace */
static int n_reorder;
static int n_remove;
static int n_add_ready;
static int n_try_suspend;
static int n_dispatch;
static int n_advance;
static int n_crash;
static int ipl_at_reorder;      /* __host_intr_disable_count seen by reorder */
static int ipl_at_dispatch;
static ec_$eventcount_t *last_advanced;

static void reset_trace(void)
{
    n_reorder = n_remove = n_add_ready = n_try_suspend = 0;
    n_dispatch = n_advance = n_crash = 0;
    ipl_at_reorder = -1;
    ipl_at_dispatch = -1;
    last_advanced = NULL;
    __host_intr_disable_count = 0;
}

void proc1_$reorder_if_needed(proc1_t *pcb)
{
    (void)pcb;
    n_reorder++;
    ipl_at_reorder = __host_intr_disable_count;
}

void proc1_$remove_from_ready_list(proc1_t *pcb) { (void)pcb; n_remove++; }
void proc1_$add_ready_body(proc1_t *pcb)         { (void)pcb; n_add_ready++; }
void proc1_$insert_into_ready_list(proc1_t *pcb) { (void)pcb; }
void PROC1_$ADD_READY(proc1_t *pcb)              { (void)pcb; n_add_ready++; }

void PROC1_$TRY_TO_SUSPEND(proc1_t *pcb)
{
    (void)pcb;
    n_try_suspend++;
}

void PROC1_$DISPATCH_INT2(proc1_t *pcb)
{
    (void)pcb;
    n_dispatch++;
    ipl_at_dispatch = __host_intr_disable_count;
}

uint16_t PROC1_$EC_WAITN(proc1_t *pcb, ec_$eventcount_t **ecs,
                         int32_t *vals, int16_t n)
{
    (void)pcb; (void)ecs; (void)vals; (void)n;
    return 0;
}

void ADVANCE_INT(ec_$eventcount_t *ec)
{
    n_advance++;
    last_advanced = ec;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    (void)status_p;
    n_crash++;
    /* ML_$UNLOCK's crash site loops forever; unwind out of the test
     * instead so the harness can report the failure. */
    assert(!"CRASH_SYSTEM called");
}

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    (void)lockp;
    return 0;
}

void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot)
{
    ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token;
    (void)lockp; (void)token;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "ml/ml_data.c"
#include "ml/exclusion_init.c"
#include "ml/exclusion_check.c"
#include "ml/cond_exclusion_start.c"
#include "ml/cond_exclusion_stop.c"
#include "ml/exclusion_stop.c"
#include "ml/unlock.c"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void reset_pcb(void)
{
    memset(&mock_pcb, 0, sizeof(mock_pcb));
    PROC1_$CURRENT_PCB = &mock_pcb;
}

static void reset_locks(void)
{
    unsigned i;
    for (i = 0; i < ML_NUM_LOCKS; i++) {
        ML_$LOCK_BYTES[i] = 0;
        ML_$LOCK_EVENTS[i].ec.value = 0;
        ML_$LOCK_EVENTS[i].ec.waiter_list_head = NULL;
        ML_$LOCK_EVENTS[i].ec.waiter_list_tail = NULL;
        ML_$LOCK_EVENTS[i].wait_count = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Exclusion-record tests (real functions)                             */
/* ------------------------------------------------------------------ */

static void test_init(void)
{
    ml_$exclusion_t excl;

    memset(&excl, 0xFF, sizeof(excl));
    ML_$EXCLUSION_INIT(&excl);

    assert(excl.f1 == 0);
    assert(excl.f2 == &excl);
    assert(excl.f3 == &excl);
    assert(excl.f4 == 0);
    assert(excl.f5 == -1);

    printf("test_init: PASSED\n");
}

static void test_check(void)
{
    ml_$exclusion_t excl;

    ML_$EXCLUSION_INIT(&excl);
    assert(ML_$EXCLUSION_CHECK(&excl) == 0);

    excl.f5 = 0;
    assert(ML_$EXCLUSION_CHECK(&excl) != 0);

    excl.f5 = 5;
    assert(ML_$EXCLUSION_CHECK(&excl) != 0);

    printf("test_check: PASSED\n");
}

static void test_cond_start_stop(void)
{
    ml_$exclusion_t excl;

    ML_$EXCLUSION_INIT(&excl);
    assert(ML_$COND_EXCLUSION_START(&excl) == 0);
    assert(excl.f5 == 0);

    /* Second attempt fails and leaves the state alone. */
    assert(ML_$COND_EXCLUSION_START(&excl) != 0);
    assert(excl.f5 == 0);

    ML_$COND_EXCLUSION_STOP(&excl);
    assert(excl.f5 == -1);
    assert(ML_$EXCLUSION_CHECK(&excl) == 0);

    printf("test_cond_start_stop: PASSED\n");
}

/* ------------------------------------------------------------------ */
/* ML_$EXCLUSION_STOP                                                  */
/* ------------------------------------------------------------------ */

/*
 * No-waiter path (0x00E20E86 blt -> 0x00E20EA2) with nesting_depth
 * dropping to zero: the original raises IPL to 7 at 0x00E20EAC before
 * running the shared tail.  The old C left `sr` uninitialised here and
 * never disabled interrupts at all.
 */
static void test_exclusion_stop_no_waiter_disables_interrupts(void)
{
    ml_$exclusion_t excl;

    reset_pcb();
    reset_trace();

    ML_$EXCLUSION_INIT(&excl);          /* f5 == -1: nobody waiting */
    excl.f5 = 0;                        /* held, no waiters */
    mock_pcb.nesting_depth = 1;
    mock_pcb.resource_locks_held = 0x00000001;

    ML_$EXCLUSION_STOP(&excl);

    assert(excl.f5 == -1);
    assert(n_advance == 0);             /* no waiters -> no ADVANCE_INT */
    assert(mock_pcb.nesting_depth == 0);
    assert(ipl_at_reorder >= 1);        /* 0x00E20EAC ori #0x700,SR */
    assert(n_reorder == 1);
    assert(n_dispatch == 1);
    assert(mock_pcb.resource_locks_held == 0);  /* 0x00E20EB0 bclr bit 0 */
    assert(__host_intr_disable_count == 0);     /* 0x00E20EEA forced IPL 0 */

    printf("test_exclusion_stop_no_waiter_disables_interrupts: PASSED\n");
}

/*
 * No-waiter path with nesting_depth still non-zero: 0x00E20EAA bne.b
 * goes straight to the rts at 0x00E20EEE, leaving the IPL untouched and
 * running none of the tail.
 */
static void test_exclusion_stop_no_waiter_nested(void)
{
    ml_$exclusion_t excl;

    reset_pcb();
    reset_trace();

    ML_$EXCLUSION_INIT(&excl);
    excl.f5 = 0;
    mock_pcb.nesting_depth = 3;
    mock_pcb.resource_locks_held = 0x00000001;

    ML_$EXCLUSION_STOP(&excl);

    assert(mock_pcb.nesting_depth == 2);
    assert(n_reorder == 0);
    assert(n_dispatch == 0);
    assert(mock_pcb.resource_locks_held == 0x00000001);
    assert(__host_intr_disable_count == 0);

    printf("test_exclusion_stop_no_waiter_nested: PASSED\n");
}

/*
 * Waiter path (0x00E20E88..0x00E20E9A): ADVANCE_INT is called on the
 * exclusion record itself, and a still-nested release exits with a
 * forced IPL 0 (0x00E20E9C), not an SR restore.
 */
static void test_exclusion_stop_waiter(void)
{
    ml_$exclusion_t excl;

    reset_pcb();
    reset_trace();

    ML_$EXCLUSION_INIT(&excl);
    excl.f5 = 2;                        /* held + 2 waiters */
    mock_pcb.nesting_depth = 2;

    ML_$EXCLUSION_STOP(&excl);

    assert(excl.f5 == 1);
    assert(n_advance == 1);
    assert(last_advanced == (ec_$eventcount_t *)&excl);
    assert(mock_pcb.nesting_depth == 1);
    assert(n_reorder == 0);             /* nesting_depth != 0 -> early rts */
    assert(__host_intr_disable_count == 0);  /* forced IPL 0 */

    printf("test_exclusion_stop_waiter: PASSED\n");
}

/* ------------------------------------------------------------------ */
/* ML_$UNLOCK                                                          */
/* ------------------------------------------------------------------ */

static void test_unlock_basic(void)
{
    reset_pcb();
    reset_trace();
    reset_locks();

    ML_$LOCK_BYTES[5] = 0x01;
    ML_$LOCK_EVENTS[5].ec.value = 7;
    ML_$LOCK_EVENTS[5].wait_count = 7;   /* no waiters */

    mock_pcb.resource_locks_held = 0x00000020;   /* bit 5 */
    mock_pcb.nesting_depth = 1;

    ML_$UNLOCK(5);

    assert(ML_$LOCK_BYTES[5] == 0x00);
    assert(n_advance == 0);
    assert(n_crash == 0);
    assert(mock_pcb.resource_locks_held == 0);
    assert(mock_pcb.nesting_depth == 0);
    assert(ipl_at_reorder >= 1);        /* 0x00E20B6A ori #0x700,SR */
    assert(n_reorder == 1);
    assert(n_dispatch == 1);
    assert(__host_intr_disable_count == 0);  /* 0x00E20EEA forced IPL 0 */

    printf("test_unlock_basic: PASSED\n");
}

static void test_unlock_wakes_waiters(void)
{
    reset_pcb();
    reset_trace();
    reset_locks();

    ML_$LOCK_BYTES[3] = 0x01;
    ML_$LOCK_EVENTS[3].ec.value = 4;
    ML_$LOCK_EVENTS[3].wait_count = 6;   /* two outstanding waits */

    mock_pcb.resource_locks_held = 0x00000008;   /* bit 3 */
    mock_pcb.nesting_depth = 1;

    ML_$UNLOCK(3);

    assert(n_advance == 1);
    assert(last_advanced == &ML_$LOCK_EVENTS[3].ec);
    /*
     * The 16-byte stride (lsl.w #4 at 0x00E20B74) is asserted at compile
     * time by the _Static_asserts on ml_$lock_event_t in ml/ml.h; on the
     * 64-bit host the pointers inside ec_$eventcount_t are wider, so only
     * the indexed access itself can be checked here.
     */
    assert(ML_$LOCK_EVENTS[3].wait_count == 6);
    assert(ML_$LOCK_EVENTS[3].ec.value == 4);

    printf("test_unlock_wakes_waiters: PASSED\n");
}

/*
 * Nested release: the lock bit is dropped but nesting_depth stays
 * non-zero, so 0x00E20EB0's `bclr.b #0,(0x43,A1)` is skipped -- yet
 * ML_$UNLOCK still falls into the shared tail (0x00E20BB2 bra.w).
 */
static void test_unlock_nested_keeps_locks_bit0(void)
{
    reset_pcb();
    reset_trace();
    reset_locks();

    ML_$LOCK_BYTES[7] = 0x01;
    mock_pcb.resource_locks_held = 0x00000081;   /* bits 0 and 7 */
    mock_pcb.nesting_depth = 2;

    ML_$UNLOCK(7);

    assert(mock_pcb.nesting_depth == 1);
    assert(mock_pcb.resource_locks_held == 0x00000001);  /* bit 0 kept */
    assert(n_reorder == 1);              /* tail still runs */
    assert(n_dispatch == 1);

    printf("test_unlock_nested_keeps_locks_bit0: PASSED\n");
}

/*
 * Deferred-suspend path.  0x00E20ED0 is `btst.b #0x2,(0x55,A1)`, i.e.
 * bit 2 (0x04) of the pri_max byte.  The audited C tested `pri_max &
 * 0x400` on a uint8_t, which is always false, so PROC1_$TRY_TO_SUSPEND
 * was never reached.
 */
static void test_unlock_deferred_suspend(void)
{
    reset_pcb();
    reset_trace();
    reset_locks();

    ML_$LOCK_BYTES[5] = 0x01;
    mock_pcb.resource_locks_held = 0x00000020;
    mock_pcb.nesting_depth = 1;
    mock_pcb.pri_max = PROC1_FLAG_DEFER_SUSP;    /* 0x04 */

    ML_$UNLOCK(5);

    assert(n_try_suspend == 1);
    assert(n_remove == 0);          /* boost bit was clear */
    assert(n_add_ready == 0);
    assert(n_dispatch == 1);

    printf("test_unlock_deferred_suspend: PASSED\n");
}

/*
 * Priority-boost path: 0x00E20EC0 `bclr.b #0x4,(0x55,A1)` clears bit 4
 * (0x10) and branches on its previous value.
 */
static void test_unlock_priority_boost(void)
{
    reset_pcb();
    reset_trace();
    reset_locks();

    ML_$LOCK_BYTES[5] = 0x01;
    mock_pcb.resource_locks_held = 0x00000020;
    mock_pcb.nesting_depth = 1;
    mock_pcb.pri_max = 0x10 | PROC1_FLAG_DEFER_SUSP;

    ML_$UNLOCK(5);

    assert((mock_pcb.pri_max & 0x10) == 0);   /* bit 4 cleared */
    assert(n_remove == 1);
    assert(n_add_ready == 1);
    assert(n_try_suspend == 1);               /* bit 2 still set */
    assert(n_dispatch == 1);

    printf("test_unlock_priority_boost: PASSED\n");
}

/*
 * When other locks remain held the flag block at 0x00E20EC0-0x00E20EE0
 * is skipped entirely (0x00E20EBA tst.l / bne).
 */
static void test_unlock_other_locks_held_skips_flags(void)
{
    reset_pcb();
    reset_trace();
    reset_locks();

    ML_$LOCK_BYTES[5] = 0x01;
    mock_pcb.resource_locks_held = 0x00000060;   /* bits 5 and 6 */
    mock_pcb.nesting_depth = 2;
    mock_pcb.pri_max = 0x10 | PROC1_FLAG_DEFER_SUSP;

    ML_$UNLOCK(5);

    assert(mock_pcb.resource_locks_held == 0x00000040);
    assert(mock_pcb.pri_max == (0x10 | PROC1_FLAG_DEFER_SUSP)); /* untouched */
    assert(n_remove == 0);
    assert(n_add_ready == 0);
    assert(n_try_suspend == 0);
    assert(n_dispatch == 1);

    printf("test_unlock_other_locks_held_skips_flags: PASSED\n");
}

/* Lock ids are masked with 0x1F by `bclr.l D0,D1` (0x00E20BA2). */
static void test_unlock_bit_number_is_mod_32(void)
{
    reset_pcb();
    reset_trace();
    reset_locks();

    ML_$LOCK_BYTES[31] = 0x01;
    mock_pcb.resource_locks_held = 0x80000000u;
    mock_pcb.nesting_depth = 1;

    ML_$UNLOCK(31);

    assert(n_crash == 0);
    assert(mock_pcb.resource_locks_held == 0);

    printf("test_unlock_bit_number_is_mod_32: PASSED\n");
}

int main(void)
{
    printf("Running ML_$ exclusion / resource lock tests...\n\n");

    test_init();
    test_check();
    test_cond_start_stop();

    test_exclusion_stop_no_waiter_disables_interrupts();
    test_exclusion_stop_no_waiter_nested();
    test_exclusion_stop_waiter();

    test_unlock_basic();
    test_unlock_wakes_waiters();
    test_unlock_nested_keeps_locks_bit0();
    test_unlock_deferred_suspend();
    test_unlock_priority_boost();
    test_unlock_other_locks_held_skips_flags();
    test_unlock_bit_number_is_mod_32();

    printf("\nAll tests PASSED!\n");
    return 0;
}
