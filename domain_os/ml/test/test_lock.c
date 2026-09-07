/*
 * Tests for ML_$LOCK (0x00E20B12).
 *
 * The test #includes the real ml/lock.c and drives the real ML_$LOCK
 * through mocked PROC1_$ dependencies.  It covers the points the
 * 2026-09-06 audit flagged for this function:
 *
 *   - the lock-ordering / PCB bookkeeping at 0x00E20AE8 is the body of the
 *     PUBLIC gate PROC1_$SET_LOCK (0x00E20AE4), not an ML-private nested
 *     procedure, and it runs exactly once per call because the retry branch
 *     at 0x00E20B54 targets 0x00E20B18 (source-fay8);
 *   - the acquire path leaves through a forced IPL 0 (`andi #-0x701,SR` at
 *     0x00E20B2C), not an SR restore;
 *   - the contended path keeps IPL 7 across PROC1_$EC_WAITN and re-raises
 *     on every retry;
 *   - the wait value handed to PROC1_$EC_WAITN is the POST-increment value
 *     of ev->wait_count (0x00E20B38 then 0x00E20B3C) and the eventcount is
 *     &ev->ec at LOCK_EVENTS + 16*id (0x00E20B36 lsl.w #4).
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
proc1_t *PROC1_$READY_PCB = NULL;

/* Call trace */
static int n_set_lock;
static uint16_t last_set_lock_id;
static int n_waitn;
static int ipl_at_waitn;
static ec_$eventcount_t *last_waitn_ec;
static int32_t last_waitn_val;
static int16_t last_waitn_n;
static proc1_t *last_waitn_pcb;

/* How many further PROC1_$EC_WAITN returns should leave the lock held. */
static int contended_rounds;
static int16_t contended_lock_id;

static void reset_trace(void)
{
    n_set_lock = 0;
    last_set_lock_id = 0xFFFF;
    n_waitn = 0;
    ipl_at_waitn = -1;
    last_waitn_ec = NULL;
    last_waitn_val = -1;
    last_waitn_n = -1;
    last_waitn_pcb = NULL;
    contended_rounds = 0;
    contended_lock_id = -1;
    __host_intr_disable_count = 0;
}

void PROC1_$SET_LOCK(uint16_t lock_id)
{
    n_set_lock++;
    last_set_lock_id = lock_id;
}

/*
 * Stands in for the blocking wait.  Each call "releases" the lock byte for
 * one more round so that ML_$LOCK's retry loop can make progress.
 */
uint16_t PROC1_$EC_WAITN(proc1_t *pcb, ec_$eventcount_t **ecs,
                         int32_t *vals, int16_t n)
{
    n_waitn++;
    ipl_at_waitn = __host_intr_disable_count;
    last_waitn_pcb = pcb;
    last_waitn_ec = ecs[0];
    last_waitn_val = vals[0];
    last_waitn_n = n;

    if (contended_rounds > 0) {
        contended_rounds--;
        if (contended_rounds == 0 && contended_lock_id >= 0) {
            ML_$LOCK_BYTES[contended_lock_id] = 0;
        }
    }
    return 0;
}

void proc1_$reorder_if_needed(proc1_t *pcb) { (void)pcb; }
void proc1_$remove_from_ready_list(proc1_t *pcb) { (void)pcb; }
void proc1_$add_ready_body(proc1_t *pcb) { (void)pcb; }
void proc1_$insert_into_ready_list(proc1_t *pcb) { (void)pcb; }
void PROC1_$ADD_READY(proc1_t *pcb) { (void)pcb; }
void PROC1_$TRY_TO_SUSPEND(proc1_t *pcb) { (void)pcb; }
void PROC1_$DISPATCH_INT2(proc1_t *pcb) { (void)pcb; }
void ADVANCE_INT(ec_$eventcount_t *ec) { (void)ec; }

void CRASH_SYSTEM(const status_$t *status_p)
{
    (void)status_p;
    assert(!"CRASH_SYSTEM called");
}

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp) { (void)lockp; return 0; }
void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    (void)lockp; (void)token;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "ml/ml_data.c"
#include "ml/lock.c"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

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

static void reset_all(void)
{
    memset(&mock_pcb, 0, sizeof(mock_pcb));
    PROC1_$CURRENT_PCB = &mock_pcb;
    reset_trace();
    reset_locks();
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * Uncontended acquire: PROC1_$SET_LOCK is called once with the caller's
 * lock id, the lock byte's bit 0 is set, nobody waits, and the function
 * leaves at IPL 0 (0x00E20B2C `andi #-0x701,SR`).
 */
static void test_lock_uncontended(void)
{
    reset_all();

    ML_$LOCK(0x0E);

    assert(n_set_lock == 1);
    assert(last_set_lock_id == 0x0E);
    assert(n_waitn == 0);
    assert(ML_$LOCK_BYTES[0x0E] == 0x01);
    /* SET_IPL0() forces the host nesting counter back to zero. */
    assert(__host_intr_disable_count == 0);

    printf("test_lock_uncontended: PASSED\n");
}

/*
 * ML_$LOCK preserves bits other than bit 0 of the lock byte: the original
 * is `bset.b #0,(0,A0,D0w)`, a read-modify-write of that one bit.
 */
static void test_lock_preserves_other_bits(void)
{
    reset_all();

    ML_$LOCK_BYTES[3] = 0xF0;

    ML_$LOCK(3);

    assert(ML_$LOCK_BYTES[3] == 0xF1);
    assert(n_waitn == 0);

    printf("test_lock_preserves_other_bits: PASSED\n");
}

/*
 * Contended acquire.  The lock byte starts held, so ML_$LOCK bumps the
 * lock's wait counter, sleeps on the lock's event count, and retries.
 *
 * Checks the arguments handed to PROC1_$EC_WAITN: &LOCK_EVENTS[id].ec,
 * the post-increment wait_count, and a count of 1 (moveq #1,D0).
 */
static void test_lock_contended_once(void)
{
    reset_all();

    ML_$LOCK_BYTES[5] = 0x01;
    ML_$LOCK_EVENTS[5].wait_count = 7;
    contended_rounds = 1;
    contended_lock_id = 5;

    ML_$LOCK(5);

    assert(n_set_lock == 1);
    assert(n_waitn == 1);
    assert(last_waitn_pcb == &mock_pcb);
    assert(last_waitn_ec == &ML_$LOCK_EVENTS[5].ec);
    assert(last_waitn_val == 8);
    assert(last_waitn_n == 1);
    assert(ML_$LOCK_EVENTS[5].wait_count == 8);
    assert(ML_$LOCK_BYTES[5] == 0x01);

    printf("test_lock_contended_once: PASSED\n");
}

/*
 * The retry branch at 0x00E20B54 goes to 0x00E20B18, NOT back to the
 * `bsr` at 0x00E20B16: the lock-ordering bookkeeping in the
 * PROC1_$SET_LOCK body must run exactly once no matter how many times
 * the acquire is retried.  Each retry re-raises to IPL 7 and bumps the
 * wait counter again.
 */
static void test_lock_set_lock_runs_once_per_call(void)
{
    reset_all();

    ML_$LOCK_BYTES[9] = 0x01;
    contended_rounds = 3;
    contended_lock_id = 9;

    ML_$LOCK(9);

    assert(n_set_lock == 1);
    assert(n_waitn == 3);
    assert(ML_$LOCK_EVENTS[9].wait_count == 3);
    assert(last_waitn_val == 3);
    assert(__host_intr_disable_count == 0);

    printf("test_lock_set_lock_runs_once_per_call: PASSED\n");
}

/*
 * The contended path never lowers the IPL before calling
 * PROC1_$EC_WAITN: there is no `andi #-0x701,SR` between 0x00E20B2A and
 * 0x00E20B4A.  The wait therefore starts with interrupts still masked.
 */
static void test_lock_waits_with_interrupts_masked(void)
{
    reset_all();

    ML_$LOCK_BYTES[1] = 0x01;
    contended_rounds = 1;
    contended_lock_id = 1;

    ML_$LOCK(1);

    assert(n_waitn == 1);
    assert(ipl_at_waitn > 0);

    printf("test_lock_waits_with_interrupts_masked: PASSED\n");
}

int main(void)
{
    printf("Running ML_$LOCK tests...\n\n");

    test_lock_uncontended();
    test_lock_preserves_other_bits();
    test_lock_contended_once();
    test_lock_set_lock_runs_once_per_call();
    test_lock_waits_with_interrupts_masked();

    printf("\nAll tests PASSED!\n");
    return 0;
}
