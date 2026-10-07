/*
 * Tests for PROC1_$RESUME (0x00E1476E), PROC1_$SUSPEND (0x00E147FA) and
 * PROC1_$INHIBIT_END (0x00E20EA2).
 *
 * The test #includes the real .c files and drives the real functions
 * through mocked dependencies.  It covers the points this pass changed:
 *
 *   - PROC1_$RESUME raises to IPL 7 at 0x00E147AC and forces IPL 0 again
 *     at 0x00E147EE, but the SUSPENDED path leaves at 0x00E147D6 WITHOUT
 *     lowering it (PROC1_$DISPATCH does that);
 *   - every flag test in PROC1_$RESUME re-reads the byte at PCB+0x55, so a
 *     `bclr` performed under the raised IPL is visible to the next test;
 *   - PROC1_$SUSPEND raises to IPL 7 at 0x00E14850 and does NOT lower it;
 *   - PROC1_$INHIBIT_END returns with the IPL untouched when the nesting
 *     depth does not reach zero (0x00E20EAA -> 0x00E20EEE), and otherwise
 *     runs the shared tail and forces IPL 0.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "base/base.h"
#include "proc1/proc1.h"

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static proc1_t pcb_table[PROC1_MAX_PROCESSES];
proc1_t *PCBS[PROC1_MAX_PROCESSES];
proc1_t *PROC1_$CURRENT_PCB;
proc1_t proc1_$pcb_pool[PROC1_MAX_PROCESSES - 1];  /* PROC1_$READY_PCB = pool[1].nextp */
uint16_t PROC1_$CURRENT = 0;

static int n_add_ready;
static int n_dispatch;
static int ipl_at_dispatch;
static int n_try_to_suspend;
static int n_dispatch_int2;
static int ipl_at_dispatch_int2;
static int n_reorder;
static int n_remove_ready;
static int n_add_ready_body;

/* When set, PROC1_$TRY_TO_SUSPEND marks the PCB suspended. */
static int try_suspend_succeeds;

void PROC1_$ADD_READY(proc1_t *pcb) { (void)pcb; n_add_ready++; }

void PROC1_$DISPATCH(void)
{
    n_dispatch++;
    ipl_at_dispatch = __host_intr_disable_count;
}

void PROC1_$TRY_TO_SUSPEND(proc1_t *pcb)
{
    n_try_to_suspend++;
    if (try_suspend_succeeds) {
        pcb->pri_max = (uint8_t)(pcb->pri_max | PROC1_FLAG_SUSPENDED);
    }
}

void PROC1_$DISPATCH_INT2(proc1_t *pcb)
{
    (void)pcb;
    n_dispatch_int2++;
    ipl_at_dispatch_int2 = __host_intr_disable_count;
}

void proc1_$reorder_if_needed(proc1_t *pcb) { (void)pcb; n_reorder++; }
void proc1_$remove_from_ready_list(proc1_t *pcb) { (void)pcb; n_remove_ready++; }
void proc1_$add_ready_body(proc1_t *pcb) { (void)pcb; n_add_ready_body++; }

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "proc1/resume.c"
#include "proc1/suspend.c"
#include "proc1/inhibit_end.c"

/* ------------------------------------------------------------------ */

static int tests_failed;

#define CHECK(cond) do {                                                \
    if (!(cond)) {                                                      \
        printf("FAILED at line %d: %s\n", __LINE__, #cond);             \
        tests_failed++;                                                 \
    }                                                                   \
} while (0)

static void reset_all(void)
{
    unsigned i;
    memset(pcb_table, 0, sizeof(pcb_table));
    for (i = 0; i < PROC1_MAX_PROCESSES; i++) {
        PCBS[i] = &pcb_table[i];
    }
    PROC1_$CURRENT_PCB = &pcb_table[1];
    n_add_ready = n_dispatch = n_try_to_suspend = 0;
    n_dispatch_int2 = n_reorder = n_remove_ready = n_add_ready_body = 0;
    ipl_at_dispatch = -1;
    ipl_at_dispatch_int2 = -1;
    try_suspend_succeeds = 0;
    __host_intr_disable_count = 0;
}

/*
 * 0x00E1477C / 0x00E1477E: pid 0 and pid > 0x40 both fail before the IPL
 * is ever raised.
 */
static void test_resume_rejects_bad_pid(void)
{
    status_$t status;

    reset_all();
    PROC1_$RESUME(0, &status);
    CHECK(status == status_$illegal_process_id);
    CHECK(__host_intr_disable_count == 0);

    reset_all();
    PROC1_$RESUME(0x41, &status);
    CHECK(status == status_$illegal_process_id);
    CHECK(__host_intr_disable_count == 0);

    printf("test_resume_rejects_bad_pid: PASSED\n");
}

/* 0x00E1479A: btst.b #0x3 -- the bound bit is 0x08 of the byte at 0x55. */
static void test_resume_requires_bound(void)
{
    status_$t status;

    reset_all();
    pcb_table[5].pri_max = PROC1_FLAG_SUSPENDED;   /* suspended but unbound */

    PROC1_$RESUME(5, &status);

    CHECK(status == status_$process_not_bound);
    CHECK(n_add_ready == 0);
    CHECK(__host_intr_disable_count == 0);

    printf("test_resume_requires_bound: PASSED\n");
}

/*
 * The suspended path: the suspended bit is cleared, the process goes back
 * on the ready list because the waiting bit is clear, PROC1_$DISPATCH runs
 * at IPL 7, and the function returns WITHOUT forcing IPL 0
 * (0x00E147D6 branches straight to the epilogue).
 */
static void test_resume_suspended_leaves_ipl_raised(void)
{
    status_$t status;

    reset_all();
    pcb_table[5].pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED;

    PROC1_$RESUME(5, &status);

    CHECK(status == status_$ok);
    CHECK((pcb_table[5].pri_max & PROC1_FLAG_SUSPENDED) == 0);
    CHECK(n_add_ready == 1);
    CHECK(n_dispatch == 1);
    CHECK(ipl_at_dispatch > 0);              /* still at IPL 7 */
    CHECK(__host_intr_disable_count > 0);    /* and stays there */

    printf("test_resume_suspended_leaves_ipl_raised: PASSED\n");
}

/*
 * 0x00E147BE: if the waiting bit is set the process is NOT put back on the
 * ready list, but PROC1_$DISPATCH still runs.
 */
static void test_resume_waiting_skips_add_ready(void)
{
    status_$t status;

    reset_all();
    pcb_table[5].pri_max =
        PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED | PROC1_FLAG_WAITING;

    PROC1_$RESUME(5, &status);

    CHECK(n_add_ready == 0);
    CHECK(n_dispatch == 1);

    printf("test_resume_waiting_skips_add_ready: PASSED\n");
}

/*
 * The deferred-suspend path: bit 2 is cleared, no dispatch happens, and
 * the function forces IPL 0 on the way out (0x00E147EE).
 */
static void test_resume_deferred_forces_ipl0(void)
{
    status_$t status;

    reset_all();
    pcb_table[5].pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_DEFER_SUSP;

    PROC1_$RESUME(5, &status);

    CHECK(status == status_$ok);
    CHECK((pcb_table[5].pri_max & PROC1_FLAG_DEFER_SUSP) == 0);
    CHECK(n_dispatch == 0);
    CHECK(__host_intr_disable_count == 0);

    printf("test_resume_deferred_forces_ipl0: PASSED\n");
}

/* 0x00E147E8: neither suspended nor deferred, and IPL 0 is still forced. */
static void test_resume_not_suspended(void)
{
    status_$t status;

    reset_all();
    pcb_table[5].pri_max = PROC1_FLAG_BOUND;

    PROC1_$RESUME(5, &status);

    CHECK(status == status_$process_not_suspended);
    CHECK(__host_intr_disable_count == 0);

    printf("test_resume_not_suspended: PASSED\n");
}

/*
 * PROC1_$SUSPEND raises the IPL at 0x00E14850 and never lowers it; the
 * result byte is re-read from PCB+0x55 after PROC1_$DISPATCH returns.
 */
static void test_suspend_leaves_ipl_raised_and_rereads(void)
{
    status_$t status;
    int8_t result;

    reset_all();
    pcb_table[7].pri_max = PROC1_FLAG_BOUND;
    try_suspend_succeeds = 1;

    result = PROC1_$SUSPEND(7, &status);

    CHECK(status == status_$ok);
    CHECK(n_try_to_suspend == 1);
    CHECK(n_dispatch == 1);
    CHECK(result == (int8_t)-1);             /* re-read saw the new bit */
    CHECK(__host_intr_disable_count > 0);

    reset_all();
    pcb_table[7].pri_max = PROC1_FLAG_BOUND;
    try_suspend_succeeds = 0;

    result = PROC1_$SUSPEND(7, &status);

    CHECK(result == 0);                      /* deferred */
    CHECK(status == status_$ok);

    printf("test_suspend_leaves_ipl_raised_and_rereads: PASSED\n");
}

/* 0x00E14848: already suspended reports the proc2-visible status. */
static void test_suspend_already_suspended(void)
{
    status_$t status;
    int8_t result;

    reset_all();
    pcb_table[7].pri_max = PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED;

    result = PROC1_$SUSPEND(7, &status);

    CHECK(status == status_$process_already_suspended);
    CHECK(result == (int8_t)-1);
    CHECK(n_try_to_suspend == 0);
    CHECK(__host_intr_disable_count == 0);

    printf("test_suspend_already_suspended: PASSED\n");
}

/*
 * PROC1_$INHIBIT_END with nesting_depth > 1 just decrements and returns
 * (0x00E20EAA -> 0x00E20EEE): no tail, no IPL change.
 */
static void test_inhibit_end_nested(void)
{
    reset_all();
    PROC1_$CURRENT_PCB = &pcb_table[1];
    pcb_table[1].nesting_depth = 2;

    PROC1_$INHIBIT_END();

    CHECK(pcb_table[1].nesting_depth == 1);
    CHECK(n_reorder == 0);
    CHECK(n_dispatch_int2 == 0);
    CHECK(__host_intr_disable_count == 0);

    printf("test_inhibit_end_nested: PASSED\n");
}

/*
 * The last INHIBIT_END clears bit 0 of resource_locks_held, runs the
 * shared tail at 0x00E20EB6 (which reaches PROC1_$DISPATCH_INT2 at IPL 7)
 * and forces IPL 0 on the way out.
 */
static void test_inhibit_end_runs_tail(void)
{
    reset_all();
    PROC1_$CURRENT_PCB = &pcb_table[1];
    pcb_table[1].nesting_depth = 1;
    pcb_table[1].resource_locks_held = 0x00000001;

    PROC1_$INHIBIT_END();

    CHECK(pcb_table[1].nesting_depth == 0);
    CHECK(pcb_table[1].resource_locks_held == 0);
    CHECK(n_reorder == 1);
    CHECK(n_dispatch_int2 == 1);
    CHECK(ipl_at_dispatch_int2 > 0);         /* the tail runs at IPL 7 */
    CHECK(__host_intr_disable_count == 0);   /* forced IPL 0 at the end */

    printf("test_inhibit_end_runs_tail: PASSED\n");
}

/*
 * The shared tail's priority-boost undo: bit 4 (0x10) of pri_max, not
 * 0x400, and bit 2 (0x04) selects the deferred suspend.
 */
static void test_inhibit_end_tail_flag_bits(void)
{
    reset_all();
    PROC1_$CURRENT_PCB = &pcb_table[1];
    pcb_table[1].nesting_depth = 1;
    pcb_table[1].resource_locks_held = 0x00000001;
    pcb_table[1].pri_max = 0x10 | PROC1_FLAG_DEFER_SUSP;

    PROC1_$INHIBIT_END();

    CHECK((pcb_table[1].pri_max & 0x10) == 0);
    CHECK(n_remove_ready == 1);
    CHECK(n_add_ready_body == 1);
    CHECK(n_try_to_suspend == 1);

    printf("test_inhibit_end_tail_flag_bits: PASSED\n");
}

int main(void)
{
    printf("Running PROC1 resume/suspend/inhibit tests...\n");

    test_resume_rejects_bad_pid();
    test_resume_requires_bound();
    test_resume_suspended_leaves_ipl_raised();
    test_resume_waiting_skips_add_ready();
    test_resume_deferred_forces_ipl0();
    test_resume_not_suspended();
    test_suspend_leaves_ipl_raised_and_rereads();
    test_suspend_already_suspended();
    test_inhibit_end_nested();
    test_inhibit_end_runs_tail();
    test_inhibit_end_tail_flag_bits();

    if (tests_failed != 0) {
        printf("%d checks failed\n", tests_failed);
        return 1;
    }
    printf("All tests PASSED!\n");
    return 0;
}
