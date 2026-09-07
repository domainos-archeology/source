/*
 * stop/test/test_watch.c - unit tests for STOP_$WATCH (0x00E81814)
 *
 * The real stop/watch.c is #included below and driven through mocks for the
 * three hand-written assembly helpers (stop/sau2/watch.s), for FIM and for
 * MST_$WIRE_AREA.  The module data normally defined by stop/stop_data.c is
 * defined here instead so each test can reset it.
 *
 * Coverage: the FIM fault return, the wire-once and calibrate-once guards,
 * the slot bound check, both start and stop paths, the negative-operation
 * unhook, the whole peek/poke branch table including the DISK_$DIAG gate,
 * the three unintended operations past the end of that table (8, 9, 10) and
 * the unbounded parent slot index.
 */

#include <stdio.h>
#include <string.h>

#include "stop/stop_internal.h"

/* ------------------------------------------------------------------ */
/* Host runtime bits the arch macros need                              */
/* ------------------------------------------------------------------ */
int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Module data (normally stop/stop_data.c)                             */
/* ------------------------------------------------------------------ */
uint32_t STOP_$SAVED_REGS[7];
int32_t STOP_$SW_OVERHEAD;
int32_t STOP_$CALIBRATION;
int32_t STOP_$TRAP_COUNTS[STOP_TRAP_COUNT_ENTRIES];
stop_$patch_rec_t STOP_$CALIB_PATCH;
m68k_ptr_t PTR_STOP_$WATCH = 0x00E81814;
int16_t STOPWATCH_WIRED;
int16_t STOPWATCH_WIRE_COUNT = 4;
stopwatch_slot_t STOPWATCH_SLOTS[STOP_MAX_SLOTS];
boolean STOP_$WATCH_TRACE_FLAG;

/* Owned by os/ and disk/, defined here for the test link */
m68k_ptr_t PTR_OS_DATA_SHUTWIRED_00e81d20 = 0x00E82128;

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */
static status_$t mock_cleanup_result = status_$cleanup_handler_set;
static int mock_cleanup_calls;
static int mock_rls_cleanup_calls;
static int mock_wire_calls;
static int mock_measure_calls;
static int32_t mock_measure_values[4];
static int mock_hook_calls;
static const stop_$patch_rec_t *mock_hook_rec;
static stopwatch_slot_t *mock_hook_slot;
static stopwatch_slot_t *mock_hook_parent;
static uint32_t mock_hook_slotno;
static int mock_unhook_calls;
static stopwatch_slot_t *mock_unhook_slot;

status_$t FIM_$CLEANUP(void *handler)
{
    (void)handler;
    mock_cleanup_calls++;
    return mock_cleanup_result;
}

void FIM_$RLS_CLEANUP(void *cleanup_data)
{
    (void)cleanup_data;
    mock_rls_cleanup_calls++;
}

void MST_$WIRE_AREA(const void *start, const void *end, void *buf1,
                    const void *param4,
                    void *buf2)
{
    (void)start;
    (void)end;
    (void)buf1;
    (void)param4;
    (void)buf2;
    mock_wire_calls++;
}

/*
 * Stands in for STOP_$MEASURE_LOOP.  Returns a canned CPU-time delta and,
 * on any call made while slot 0 is hooked, adds mock_measure_accum to that
 * slot's cpu_time -- which is what the real trace handler does when the
 * loop's 1024 calls hit the patched STOP_$NULL_PROC.
 */
static int32_t mock_measure_accum;

int32_t stop_$measure_loop(void)
{
    int32_t v = mock_measure_values[mock_measure_calls & 3];
    mock_measure_calls++;
    if ((STOPWATCH_SLOTS[0].flags & STOP_SLOT_RUNNING) != 0) {
        STOPWATCH_SLOTS[0].cpu_time += mock_measure_accum;
    }
    return v;
}

void stop_$hook(const stop_$patch_rec_t *rec, stopwatch_slot_t *slot,
                stopwatch_slot_t *parent, uint32_t slotno)
{
    mock_hook_calls++;
    mock_hook_rec = rec;
    mock_hook_slot = slot;
    mock_hook_parent = parent;
    mock_hook_slotno = slotno;
    /* The real helper zeroes the slot and sets the running bit. */
    memset(slot, 0, sizeof(*slot));
    slot->parent = parent;
    slot->flags |= STOP_SLOT_RUNNING;
}

void stop_$unhook(stopwatch_slot_t *slot)
{
    mock_unhook_calls++;
    mock_unhook_slot = slot;
    slot->flags &= (uint8_t)~STOP_SLOT_RUNNING;
}

/* ------------------------------------------------------------------ */
/* The code under test                                                 */
/* ------------------------------------------------------------------ */
#include "stop/watch.c"

/* DISK_$DIAG, DISK_$DO_CHKSUM and the module exclusion lock are cells of the
 * DISK_ module block (disk/disk.h), so the host build provides the block. */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */
static int tests_run;
static int tests_failed;
static int current_failed;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("\n    FAIL line %d: %s", __LINE__, #cond);                 \
            current_failed = 1;                                                \
        }                                                                      \
    } while (0)

#define CHECK_EQ(expected, actual)                                             \
    do {                                                                       \
        long long e_ = (long long)(expected);                                  \
        long long a_ = (long long)(actual);                                    \
        if (e_ != a_) {                                                        \
            printf("\n    FAIL line %d: %s: expected 0x%llx, got 0x%llx",      \
                   __LINE__, #actual, (unsigned long long)e_,                  \
                   (unsigned long long)a_);                                    \
            current_failed = 1;                                                \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        printf("  %-40s", #fn);                                                \
        current_failed = 0;                                                    \
        tests_run++;                                                           \
        reset_state();                                                         \
        fn();                                                                  \
        if (current_failed) {                                                  \
            tests_failed++;                                                    \
            printf("\n  %-40s FAILED\n", #fn);                                 \
        } else {                                                               \
            printf(" ok\n");                                                   \
        }                                                                      \
    } while (0)

static void reset_state(void)
{
    memset(STOPWATCH_SLOTS, 0, sizeof(STOPWATCH_SLOTS));
    memset(STOP_$TRAP_COUNTS, 0, sizeof(STOP_$TRAP_COUNTS));
    STOP_$CALIBRATION = 1; /* pretend calibration already happened */
    STOP_$SW_OVERHEAD = 0;
    STOPWATCH_WIRED = 1;   /* pretend the region is already wired */
    DISK_$DIAG = 0;
    mock_cleanup_result = status_$cleanup_handler_set;
    mock_cleanup_calls = 0;
    mock_rls_cleanup_calls = 0;
    mock_wire_calls = 0;
    mock_measure_calls = 0;
    mock_measure_values[0] = 0;
    mock_measure_values[1] = 0;
    mock_measure_values[2] = 0;
    mock_measure_values[3] = 0;
    mock_measure_accum = 0;
    mock_hook_calls = 0;
    mock_hook_rec = NULL;
    mock_hook_slot = NULL;
    mock_hook_parent = NULL;
    mock_hook_slotno = 0xFFFFFFFFu;
    mock_unhook_calls = 0;
    mock_unhook_slot = NULL;
    __host_intr_disable_count = 0;
}

/* Convenience wrapper: everything STOP_$WATCH takes is by reference. */
static status_$t call_watch(int16_t op, uint16_t slot, int16_t parent, void *p4,
                            void *p5)
{
    status_$t st = 0x0BADF00D;
    STOP_$WATCH(&op, &slot, &parent, p4, p5, &st);
    return st;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* 0x00E8182E-0x00E81834: a fault return skips everything else. */
static void test_fim_fault_return(void)
{
    status_$t st;
    stop_$patch_rec_t rec = {NULL, NULL};

    mock_cleanup_result = 0x00120099;
    st = call_watch(STOP_OP_START, 0, -1, &rec, NULL);

    CHECK_EQ(0x00120099, st);
    CHECK_EQ(1, mock_cleanup_calls);
    CHECK_EQ(0, mock_rls_cleanup_calls); /* no release on the fault path */
    CHECK_EQ(0, mock_hook_calls);
}

/* 0x00E8189C-0x00E818C0: MST_$WIRE_AREA runs only while STOPWATCH_WIRED == 0 */
static void test_wire_once(void)
{
    stop_$patch_rec_t rec = {NULL, NULL};

    STOPWATCH_WIRED = 0;
    (void)call_watch(STOP_OP_START, 0, -1, &rec, NULL);
    CHECK_EQ(1, mock_wire_calls);

    /* The mock does not set the flag, so the guard is what we are testing:
     * with the flag set, no second call. */
    STOPWATCH_WIRED = 1;
    STOPWATCH_SLOTS[0].flags = 0;
    (void)call_watch(STOP_OP_START, 0, -1, &rec, NULL);
    CHECK_EQ(1, mock_wire_calls);
}

/*
 * 0x00E818C4-0x00E81900: STOP_$CALIBRATION doubles as the "done" flag, and
 * only the low word of each of the two calibration cells is written.
 */
static void test_calibration_once(void)
{
    stop_$patch_rec_t rec = {NULL, NULL};

    STOP_$CALIBRATION = 0;
    STOP_$SW_OVERHEAD = (int32_t)0xAAAA0000;
    /* base = 0x1000, instrumented = 0x1000 + 0x800 * 7 -> 7 per event */
    mock_measure_values[0] = 0x1000;
    mock_measure_values[1] = 0x1000 + 0x800 * 7;
    /* the instrumented pass accumulates 0x400 * 5 into slot 0 -> 5/event */
    mock_measure_accum = 0x400 * 5;

    (void)call_watch(STOP_OP_START, 1, -1, &rec, NULL);

    CHECK_EQ(2, mock_measure_calls);
    CHECK_EQ(7, STOP_$CALIBRATION);
    /* the high half of STOP_$SW_OVERHEAD must survive */
    CHECK_EQ((int32_t)0xAAAA0005, STOP_$SW_OVERHEAD);
    /* slot 0 hooked for the measurement, then unhooked */
    CHECK_EQ(&STOPWATCH_SLOTS[0], mock_unhook_slot);
    CHECK_EQ(1, mock_unhook_calls);

    /* second call: calibration is non-zero now, so it is skipped */
    mock_measure_calls = 0;
    STOPWATCH_SLOTS[1].flags = 0;
    (void)call_watch(STOP_OP_START, 1, -1, &rec, NULL);
    CHECK_EQ(0, mock_measure_calls);
}

/* 0x00E81908: `cmp.w #0xf,D0w / bls` -- unsigned bound at 15 */
static void test_slot_bounds(void)
{
    stop_$patch_rec_t rec = {NULL, NULL};

    CHECK_EQ(status_$stop_bad_slot, call_watch(STOP_OP_START, 16, -1, &rec, NULL));
    CHECK_EQ(0, mock_hook_calls);

    CHECK_EQ(status_$stop_bad_slot,
             call_watch(STOP_OP_START, 0xFFFF, -1, &rec, NULL));
    CHECK_EQ(0, mock_hook_calls);

    /* 15 is in range */
    CHECK_EQ(status_$ok, call_watch(STOP_OP_START, 15, -1, &rec, NULL));
    CHECK_EQ(1, mock_hook_calls);
    CHECK_EQ(&STOPWATCH_SLOTS[15], mock_hook_slot);

    /* the bound applies to the stop path too (0x00E81902 is shared) */
    CHECK_EQ(status_$stop_bad_slot, call_watch(STOP_OP_STOP, 16, -1, NULL, NULL));

    /* every call released the cleanup handler */
    CHECK_EQ(4, mock_rls_cleanup_calls);
}

/* 0x00E8194E-0x00E81992: start */
static void test_start(void)
{
    stop_$patch_rec_t rec = {(uint16_t *)0x1234, (uint16_t *)0x5678};

    CHECK_EQ(status_$ok, call_watch(STOP_OP_START, 3, -1, &rec, NULL));
    CHECK_EQ(1, mock_hook_calls);
    CHECK(mock_hook_rec == &rec);
    CHECK_EQ(&STOPWATCH_SLOTS[3], mock_hook_slot);
    CHECK(mock_hook_parent == NULL); /* parent < 0 -> none */
    CHECK_EQ(3, mock_hook_slotno);

    /* a non-negative parent selects a slot */
    STOPWATCH_SLOTS[4].flags = 0;
    CHECK_EQ(status_$ok, call_watch(STOP_OP_START, 4, 3, &rec, NULL));
    CHECK_EQ(&STOPWATCH_SLOTS[3], mock_hook_parent);
    CHECK_EQ(4, mock_hook_slotno);
}

/* 0x00E81956: starting a slot that is already running */
static void test_start_already_running(void)
{
    stop_$patch_rec_t rec = {NULL, NULL};

    STOPWATCH_SLOTS[2].flags = STOP_SLOT_RUNNING;
    CHECK_EQ(status_$stop_already_running,
             call_watch(STOP_OP_START, 2, -1, &rec, NULL));
    CHECK_EQ(0, mock_hook_calls);
}

/* 0x00E8199E: stopping a slot that is not running */
static void test_stop_not_running(void)
{
    stop_$data_t out;

    memset(&out, 0xEE, sizeof(out));
    CHECK_EQ(status_$stop_bad_slot, call_watch(STOP_OP_STOP, 5, -1, NULL, &out));
    CHECK_EQ(0, mock_unhook_calls);
}

/*
 * 0x00E819A2-0x00E819D8: harvest.  The per-event calibration is charged
 * against each accumulator using only the low words, the two longword event
 * counters are zeroed, and four longwords are copied out and zeroed.
 */
static void test_stop_harvest(void)
{
    stopwatch_slot_t *sl = &STOPWATCH_SLOTS[7];
    stop_$data_t out;

    STOP_$CALIBRATION = 0x00010003; /* low word 3 is what mulu.w uses */
    sl->flags = STOP_SLOT_RUNNING;
    sl->completions = 11;
    sl->reentries = 22;
    sl->cpu_time = 1000;
    sl->elapsed_time = 2000;
    sl->cpu_events = 0x00010004;     /* low word 4 -> 4 * 3 = 12 charged */
    sl->elapsed_events = 5;          /* 5 * 3 = 15 charged */
    memset(&out, 0xEE, sizeof(out));

    CHECK_EQ(status_$ok, call_watch(STOP_OP_STOP, 7, -1, NULL, &out));

    CHECK_EQ(11, out.completions);
    CHECK_EQ(22, out.reentries);
    CHECK_EQ(1000 - 12, out.cpu_time);
    CHECK_EQ(2000 - 15, out.elapsed_time);

    /* the four harvested longwords are cleared in the slot */
    CHECK_EQ(0, sl->completions);
    CHECK_EQ(0, sl->reentries);
    CHECK_EQ(0, sl->cpu_time);
    CHECK_EQ(0, sl->elapsed_time);
    /* both event counters are cleared as whole longwords */
    CHECK_EQ(0, sl->cpu_events);
    CHECK_EQ(0, sl->elapsed_events);

    /* operation 0 leaves the slot hooked (0x00E819DA: tst.w D3 / beq) */
    CHECK_EQ(0, mock_unhook_calls);
    CHECK_EQ(STOP_SLOT_RUNNING, sl->flags);

    /* the SR save/restore is balanced */
    CHECK_EQ(0, __host_intr_disable_count);
}

/* 0x00E819DE: a negative operation also unhooks */
static void test_stop_negative_unhooks(void)
{
    stopwatch_slot_t *sl = &STOPWATCH_SLOTS[8];
    stop_$data_t out;

    sl->flags = STOP_SLOT_RUNNING;
    CHECK_EQ(status_$ok, call_watch(-1, 8, -1, NULL, &out));
    CHECK_EQ(1, mock_unhook_calls);
    CHECK_EQ(sl, mock_unhook_slot);
}

/* 0x00E8187E / 0x00E81888 / 0x00E81892: the three peeks, ungated */
static void test_peek(void)
{
    static uint8_t mem[8];
    void *addr_cell;      /* the longword at (0x14,A6) holds an address */
    uint32_t value_cell;

    addr_cell = mem;
    DISK_$DIAG = 0; /* peeks are not gated */

    /* Each case writes the buffer with the same width it will be read at,
     * so the test says nothing about host byte order. */
    memset(mem, 0, sizeof(mem));
    mem[0] = 0xDE;
    value_cell = 0x11111111;
    CHECK_EQ(status_$ok,
             call_watch(STOP_OP_PEEK_BYTE, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0xDEu, value_cell); /* zero extended: D2 was cleared first */

    memset(mem, 0, sizeof(mem));
    *(uint16_t *)mem = 0xBEEFu;
    value_cell = 0x11111111;
    CHECK_EQ(status_$ok,
             call_watch(STOP_OP_PEEK_WORD, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0xBEEFu, value_cell); /* zero extended into the full longword */

    memset(mem, 0, sizeof(mem));
    *(uint32_t *)mem = 0xDEADBEEFu;
    value_cell = 0x11111111;
    CHECK_EQ(status_$ok,
             call_watch(STOP_OP_PEEK_LONG, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0xDEADBEEFu, value_cell);

    /* three calls, three cleanup releases */
    CHECK_EQ(3, mock_rls_cleanup_calls);
}

/* 0x00E8186A: pokes are refused unless DISK_$DIAG is non-zero */
static void test_poke_gated(void)
{
    static uint8_t mem[8];
    void *addr_cell;      /* the longword at (0x14,A6) holds an address */
    uint32_t value_cell;

    memset(mem, 0, sizeof(mem));
    addr_cell = mem;

    DISK_$DIAG = 0;
    value_cell = 0x99;
    CHECK_EQ(status_$stop_not_diag,
             call_watch(STOP_OP_POKE_BYTE, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0, mem[0]);

    value_cell = 0x1122;
    CHECK_EQ(status_$stop_not_diag,
             call_watch(STOP_OP_POKE_WORD, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0, mem[0]);

    value_cell = 0x11223344;
    CHECK_EQ(status_$stop_not_diag,
             call_watch(STOP_OP_POKE_LONG, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0, mem[0]);

    /* the escape still releases the cleanup handler (0x00E81874 -> 0x00E8195C) */
    CHECK_EQ(3, mock_rls_cleanup_calls);
}

static void test_poke_allowed(void)
{
    static uint8_t mem[8];
    void *addr_cell;      /* the longword at (0x14,A6) holds an address */
    uint32_t value_cell;

    memset(mem, 0, sizeof(mem));
    addr_cell = mem;
    DISK_$DIAG = 1; /* `tst.b / bne`: any non-zero enables */

    value_cell = 0x99;
    CHECK_EQ(status_$ok,
             call_watch(STOP_OP_POKE_BYTE, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0x99, mem[0]);
    CHECK_EQ(0, mem[1]);

    memset(mem, 0, sizeof(mem));
    value_cell = 0x1122;
    CHECK_EQ(status_$ok,
             call_watch(STOP_OP_POKE_WORD, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0x1122u, *(uint16_t *)mem);

    memset(mem, 0, sizeof(mem));
    value_cell = 0x11223344;
    CHECK_EQ(status_$ok,
             call_watch(STOP_OP_POKE_LONG, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0x11223344u, *(uint32_t *)mem);

    /* a poke never touches the value cell */
    CHECK_EQ(0x11223344u, value_cell);
}

/*
 * source-3ena: the operation code has NO upper bound (0x00E8183E is
 * `cmp.w #1,D3 / ble` and nothing more), and operation 7's table entry is a
 * four-byte `bsr.w`, so codes 8, 9 and 10 land on real instructions inside
 * the table's island.  8 and 10 are no-ops; 9 is a long poke that never
 * reaches the DISK_$DIAG gate.
 */
static void test_op_above_table(void)
{
    static uint8_t mem[8];
    void *addr_cell;
    uint32_t value_cell;

    memset(mem, 0, sizeof(mem));
    addr_cell = mem;
    DISK_$DIAG = 0; /* diagnostics DISABLED for all of this test */

    /* 8: `ori.b #0x81,D6` then the common exit -- no memory touched */
    value_cell = 0xAABBCCDDu;
    CHECK_EQ(status_$ok,
             call_watch(STOP_OP_ORI_D6, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0u, *(uint32_t *)mem);

    /* 10: the common exit branch itself -- also a no-op */
    CHECK_EQ(status_$ok,
             call_watch(STOP_OP_NOP, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0u, *(uint32_t *)mem);

    /*
     * 9 (0x00E81866): `move.l D1,(A1)` with the gate skipped, so it writes
     * even though DISK_$DIAG is zero -- unlike operation 7, which refuses.
     */
    CHECK_EQ(status_$ok, call_watch(STOP_OP_POKE_LONG_UNGATED, 0, 0,
                                    &addr_cell, &value_cell));
    CHECK_EQ(0xAABBCCDDu, *(uint32_t *)mem);

    /* the gated long poke with the same state refuses and writes nothing */
    memset(mem, 0, sizeof(mem));
    CHECK_EQ(status_$stop_not_diag,
             call_watch(STOP_OP_POKE_LONG, 0, 0, &addr_cell, &value_cell));
    CHECK_EQ(0u, *(uint32_t *)mem);
}

/*
 * source-3ena: the parent slot index is guarded only by `blt` (0x00E81988),
 * so any non-negative value is scaled and used.  The scaling is a 16-bit
 * `lsl.w #6` and the base is reached with a sign-extending `adda.w`, so
 * 0x200..0x3FF walk backwards from &STOPWATCH_SLOTS[0] and 0x400 wraps to
 * exactly the base.
 */
static void test_parent_unbounded(void)
{
    stop_$patch_rec_t rec = {(uint16_t *)0x1234, NULL};
    stopwatch_slot_t *base = &STOPWATCH_SLOTS[0];

    /* past the end of the 16-slot table, still used */
    CHECK_EQ(status_$ok, call_watch(STOP_OP_START, 0, 20, &rec, NULL));
    CHECK(mock_hook_parent == base + 20);

    /* 0x200 * 0x40 = 0x8000: negative once sign-extended by adda.w */
    STOPWATCH_SLOTS[1].flags = 0;
    CHECK_EQ(status_$ok, call_watch(STOP_OP_START, 1, 0x200, &rec, NULL));
    CHECK(mock_hook_parent == base - 0x200);

    /* 0x400 * 0x40 wraps the word to 0, landing back on slot 0 */
    STOPWATCH_SLOTS[2].flags = 0;
    CHECK_EQ(status_$ok, call_watch(STOP_OP_START, 2, 0x400, &rec, NULL));
    CHECK(mock_hook_parent == base);

    /* 0x401 comes back as slot 1, not slot 0x401 */
    STOPWATCH_SLOTS[3].flags = 0;
    CHECK_EQ(status_$ok, call_watch(STOP_OP_START, 3, 0x401, &rec, NULL));
    CHECK(mock_hook_parent == base + 1);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("STOP_$WATCH (0x00E81814) tests\n");

    RUN(test_fim_fault_return);
    RUN(test_wire_once);
    RUN(test_calibration_once);
    RUN(test_slot_bounds);
    RUN(test_start);
    RUN(test_start_already_running);
    RUN(test_stop_not_running);
    RUN(test_stop_harvest);
    RUN(test_stop_negative_unhooks);
    RUN(test_peek);
    RUN(test_poke_gated);
    RUN(test_poke_allowed);
    RUN(test_op_above_table);
    RUN(test_parent_unbounded);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
