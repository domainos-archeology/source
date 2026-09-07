/*
 * suma/test/test_suma_data.c - Unit tests for suma/suma_data.c: SUMA_$STATE
 * (0x00E2DD88) and the DXM callback cell at 0x00E1AECC.
 *
 * suma/suma_data.c and suma/init.c are #included below and the real
 * functions are called.
 *
 * TERM_$ENQUEUE_TPAD is stubbed rather than #included: term/enqueue_tpad.c
 * pulls in term/term_internal.h -> sio/sio.h, whose
 * "_Static_assert(sizeof(sio_desc_t) == 0x78)" is not guarded by ARCH_M68K
 * and so fails on a 64-bit host (bead source-0ut7).  The cell test still
 * proves what the bead is about - that suma_data.c binds the cell to the
 * symbol TERM_$ENQUEUE_TPAD and not to something else.
 *
 * Facts under test (bead source-f4qo):
 *   - PTR_TERM_$ENQUEUE_TPAD_00e1aecc is the ADDRESS of a cell, not a
 *     function pointer: SUMA_$RCV reaches it with 0x00E1AE90 "pea (0x3a,PC)",
 *     i.e. 0x00E1AE90 + 2 + 0x3A = 0x00E1AECC
 *   - the image holds 00 e7 24 72 there, so the cell must resolve to
 *     TERM_$ENQUEUE_TPAD at 0x00E72472
 *   - it is a SEPARATE cell from KBD's PTR_TERM_$ENQUEUE_TPAD_00e1ce90
 *     (0x00E1CE90, defined in term/term_data.c); each module carries its own
 *     literal, and both happen to hold the same code address
 *   - SUMA_$STATE is a real object again (it had no definition at all):
 *     SUMA_$INIT (0x00E33224) writes tpad_buffer, rcv_state, cur_id_flags
 *     and threshold into it by absolute address 0x00E2DD88
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "suma/suma_internal.h"
#include "term/term.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-44s", #name);                                             \
        current_failed = 0;                                                   \
        test_##name();                                                        \
        if (current_failed) {                                                 \
            tests_failed++;                                                   \
        } else {                                                              \
            tests_passed++;                                                   \
            printf("PASSED\n");                                               \
        }                                                                     \
    } while (0)

#define CHECK_EQ(expected, actual)                                            \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            if (!current_failed) printf("FAILED\n");                          \
            current_failed = 1;                                               \
            printf("      %s:%d: %s: expected 0x%llx, got 0x%llx\n",          \
                   __FILE__, __LINE__, #actual,                               \
                   (unsigned long long)_e, (unsigned long long)_a);           \
        }                                                                     \
    } while (0)

/*
 * suma_state_t carries a native pointer (tpad_buffer), so on a 64-bit host it
 * is both wider and 8-byte aligned: only the fields BELOW it keep their image
 * offsets.  The rest are asserted in the m68k build only.
 */
_Static_assert(offsetof(suma_state_t, cur_id_flags) == 0x1E,
               "SUMA_$INIT 0x00E3323A move.b #0x1,(0x1e,A0)");
#if defined(ARCH_M68K)
_Static_assert(offsetof(suma_state_t, tpad_buffer) == 0x24,
               "SUMA_$INIT 0x00E3322E move.l #0xe2de3c,(0x24,A0)");
_Static_assert(offsetof(suma_state_t, rcv_state) == 0x28,
               "SUMA_$INIT 0x00E33236 clr.w (0x28,A0)");
_Static_assert(offsetof(suma_state_t, threshold) == 0x2A,
               "SUMA_$INIT 0x00E33240 move.w #0x200,(0x2a,A0)");
#endif

/* ------------------------------------------------------------------ */
/* Globals and harness plumbing                                        */
/* ------------------------------------------------------------------ */

tpad_buffer_t TERM_$TPAD_BUFFER;

/*
 * KBD's twin cell at 0x00E1CE90, defined here exactly the way
 * term/term_data.c defines it.  Pulling term_data.c in would drag the whole
 * TERM subsystem (and sio/sio.h, see the header comment) into this test, but
 * the definition is one line and the point of the comparison below is that
 * the two literals are separate objects that happen to hold the same code
 * address (0x00E72472).
 */
DXM_$DEFINE_CALLBACK_CELL(PTR_TERM_$ENQUEUE_TPAD_00e1ce90, TERM_$ENQUEUE_TPAD);

/*
 * Host-side callback-cell registry.  dxm/dxm_data.c owns the real one, but
 * pulling that translation unit in would drag DXM_$ADD_SIGNAL_CALLBACK and
 * the PROC2 signal routines into this test, so the two entry points are
 * reimplemented here with the same contract (see dxm/dxm.h): a cell is a
 * 1-based handle, 0 means "unregistered".
 */
static dxm_$callback_fn_t host_callbacks[DXM_HOST_CALLBACK_MAX];
static uint32_t host_callback_count;

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    uint32_t i;

    for (i = 0; i < host_callback_count; i++) {
        if (host_callbacks[i] == fn) {
            return (dxm_$callback_t)(i + 1);
        }
    }
    if (host_callback_count >= DXM_HOST_CALLBACK_MAX) {
        return 0;
    }
    host_callbacks[host_callback_count] = fn;
    host_callback_count++;
    return (dxm_$callback_t)host_callback_count;
}

dxm_$callback_fn_t dxm_$callback_fn(dxm_$callback_t cell)
{
    if (cell == 0 || cell > host_callback_count) {
        return NULL;
    }
    return host_callbacks[cell - 1];
}

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static int n_enqueue_tpad;
static void *last_enqueue_arg;

/*
 * Stands in for term/enqueue_tpad.c (0x00E72472).  suma_data.c only ever
 * takes its address, so the stub is enough to prove the binding; the drain
 * behaviour itself belongs to a term/ test.
 */
void TERM_$ENQUEUE_TPAD(void **param1)
{
    n_enqueue_tpad++;
    last_enqueue_arg = *param1;
}

/* ------------------------------------------------------------------ */
/* Modules under test                                                  */
/* ------------------------------------------------------------------ */

#include "../suma_data.c"
#include "../init.c"

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

TEST(cell_resolves_to_enqueue_tpad)
{
    /* Image bytes at 0x00E1AECC: 00 e7 24 72 = TERM_$ENQUEUE_TPAD. */
    CHECK_EQ((uintptr_t)(void *)TERM_$ENQUEUE_TPAD,
             (uintptr_t)(void *)dxm_$callback_fn(
                 PTR_TERM_$ENQUEUE_TPAD_00e1aecc));
}

TEST(cell_is_distinct_storage_from_kbds)
{
    /*
     * 0x00E1AECC and 0x00E1CE90 are two separate literals in two separate
     * code regions, so they must be two separate objects even though they
     * hold the same value.
     */
    CHECK_EQ(0, (uintptr_t)&PTR_TERM_$ENQUEUE_TPAD_00e1aecc ==
                (uintptr_t)&PTR_TERM_$ENQUEUE_TPAD_00e1ce90);
    CHECK_EQ(PTR_TERM_$ENQUEUE_TPAD_00e1ce90,
             PTR_TERM_$ENQUEUE_TPAD_00e1aecc);
}

TEST(init_seeds_the_state_record)
{
    memset(&SUMA_$STATE, 0xAA, sizeof(SUMA_$STATE));

    SUMA_$INIT();

    /* 00e3322e move.l #0xe2de3c,(0x24,A0) */
    CHECK_EQ((uintptr_t)&TERM_$TPAD_BUFFER, (uintptr_t)SUMA_$STATE.tpad_buffer);
    /* 00e33236 clr.w (0x28,A0) */
    CHECK_EQ(0, SUMA_$STATE.rcv_state);
    /* 00e3323a move.b #0x1,(0x1e,A0) */
    CHECK_EQ(1, SUMA_$STATE.cur_id_flags);
    /* 00e33240 move.w #0x200,(0x2a,A0) */
    CHECK_EQ(SUMA_INITIAL_THRESHOLD, SUMA_$STATE.threshold);
}

TEST(callback_receives_the_buffer_pointer_cell)
{
    void *datum;

    /*
     * SUMA_$RCV pushes a cell holding &SUMA_$STATE.tpad_buffer
     * (0x00E1AE84 "lea (0x24,A5),A0" / 0x00E1AE88 "move.l A0,(-0x18,A6)" /
     * 0x00E1AE8C "pea (-0x18,A6)"), so what reaches the callback is the
     * address of a cell that holds the buffer pointer - one indirection more
     * than the buffer itself.
     */
    SUMA_$INIT();
    n_enqueue_tpad = 0;
    last_enqueue_arg = NULL;

    datum = &SUMA_$STATE.tpad_buffer;
    dxm_$callback_fn(PTR_TERM_$ENQUEUE_TPAD_00e1aecc)(&datum);

    CHECK_EQ(1, n_enqueue_tpad);
    CHECK_EQ((uintptr_t)&SUMA_$STATE.tpad_buffer, (uintptr_t)last_enqueue_arg);
    CHECK_EQ((uintptr_t)&TERM_$TPAD_BUFFER,
             (uintptr_t)*(void **)last_enqueue_arg);
}

int main(void)
{
    printf("test_suma_data:\n");

    RUN_TEST(cell_resolves_to_enqueue_tpad);
    RUN_TEST(cell_is_distinct_storage_from_kbds);
    RUN_TEST(init_seeds_the_state_record);
    RUN_TEST(callback_receives_the_buffer_pointer_cell);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
