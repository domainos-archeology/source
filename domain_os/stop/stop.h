/*
 * stop/stop.h - Stopwatch Profiling Subsystem
 *
 * STOP_$WATCH instruments an arbitrary pair of code addresses by patching
 * A-line (0xAxxx) trap words over the instructions found there.  The
 * resulting unimplemented-instruction traps are taken by STOP_$WATCH_UII
 * (0x00E81A56), which hands off to STOP_$WATCH_TRACE (0x00E81AB2); the
 * trace handler accumulates CPU time (PROC1_$GET_CPUT) and wall time
 * (TIME_$CLOCK) into one of 16 stopwatch slots.
 *
 * The same entry point also implements a privileged physical peek/poke
 * (operations 2..7), reached through a branch table at 0x00E81854 and
 * gated for the poke operations by DISK_$DIAG.
 *
 * Module base (A5) is the entry point itself: A5 = 0x00E81814.
 *
 * Original address: 0x00E81814 (352 bytes)
 */

#ifndef STOP_H
#define STOP_H

#include "base/base.h"

/*
 * Maximum number of stopwatch slots.
 *
 * The bound is enforced at 0x00E81908 as `cmp.w #0xf,D0w / bls`, i.e. an
 * unsigned compare against 15.
 */
#define STOP_MAX_SLOTS 16

/*
 * Operation codes (first parameter, a word passed by reference).
 *
 * 0 and 1 are handled inline; everything greater than 1 is dispatched
 * through the two-byte branch table at 0x00E81854
 * (`jmp (0xe81854,PC,D3.w)` with D3 = 2 * operation).
 */
#define STOP_OP_STOP 0      /* stop the slot and return its totals */
#define STOP_OP_START 1     /* start the slot */
#define STOP_OP_PEEK_BYTE 2 /* 0x00E8187E */
#define STOP_OP_POKE_BYTE 3 /* 0x00E81882 (DISK_$DIAG gated) */
#define STOP_OP_PEEK_WORD 4 /* 0x00E81888 */
#define STOP_OP_POKE_WORD 5 /* 0x00E8188C (DISK_$DIAG gated) */
#define STOP_OP_PEEK_LONG 6 /* 0x00E81892 */
#define STOP_OP_POKE_LONG 7 /* 0x00E81862 (DISK_$DIAG gated) */

/*
 * There is no upper bound on the operation code: 0x00E8183E is
 * `cmp.w #1,D3 / ble` and nothing else, so the jump can land anywhere.
 * Operation 7's entry is a four-byte `bsr.w`, not a two-byte `bra.b`, so
 * three more codes land on real instruction boundaries inside the table's
 * own island and have defined -- if plainly unintended -- behaviour:
 *
 *   8  0x00E81864  `ori.b #0x81,D6` on a register the exit movem restores,
 *                  then a branch to the common exit: a no-op returning ok.
 *   9  0x00E81866  `move.l D1,(A1)`: a long poke that never passes the
 *                  DISK_$DIAG gate operation 7 goes through.
 *  10  0x00E81868  the common exit branch itself: a no-op returning ok.
 *
 * Operation 11 lands on the gate at 0x00E8186A, entered by `jmp` instead of
 * `bsr`, so its `rts` returns to STOP_$WATCH's caller with the frame still
 * linked and its refusal path pops the caller's return address; 12 and above
 * land in the middle of instructions.  STOP_OP_MAX_DEFINED is the last code
 * a translation can reproduce.
 */
#define STOP_OP_ORI_D6 8            /* 0x00E81864, no-op */
#define STOP_OP_POKE_LONG_UNGATED 9 /* 0x00E81866, NOT DISK_$DIAG gated */
#define STOP_OP_NOP 10              /* 0x00E81868, no-op */
#define STOP_OP_MAX_DEFINED 10

/*
 * Status codes returned by STOP_$WATCH (subsystem byte 0x30).
 *
 * Note: audit/ also claims subsystem 0x30; these three codes are the ones
 * the STOP_$WATCH code itself loads into D2.
 */
#define status_$stop_bad_slot 0x00300001    /* 0x00E8190E: slot > 15, or */
                                            /* stop of a slot not running */
#define status_$stop_already_running 0x00300002 /* 0x00E81956 */
#define status_$stop_not_diag 0x00300004     /* 0x00E81874: poke refused */

/*
 * Accumulated stopwatch data returned by operation 0.
 *
 * The stop path copies four longwords out of the slot starting at slot+0x14
 * (0x00E819CA-0x00E819D4: `lea (0x14,A1),A2 / moveq #3,D1 / move.l (A2),(A3)+
 * / clr.l (A2)+ / dbf`), zeroing each as it goes.
 */
typedef struct stop_$data_t {
    int32_t completions;   /* slot+0x14: measured intervals completed */
    int32_t reentries;     /* slot+0x18: traps taken while already running */
    int32_t cpu_time;      /* slot+0x1C: accumulated PROC1_$GET_CPUT delta */
    int32_t elapsed_time;  /* slot+0x20: accumulated TIME_$CLOCK delta */
} stop_$data_t;

/*
 * A patch record: the two code addresses whose instruction words
 * STOP_$WATCH replaces with A-line traps.  Read by the hook helper with
 * `movem.l (A0),{A3,A4}` at 0x00E81A18; a NULL second address means the
 * slot has only an entry point (flag bit 6 stays clear).
 */
typedef struct stop_$patch_rec_t {
    uint16_t *entry_addr; /* +0x00: patched with 0xA000 + slot */
    uint16_t *exit_addr;  /* +0x04: patched with 0xA100 + slot, or NULL */
} stop_$patch_rec_t;

/*
 * STOP_$WATCH - stopwatch control and privileged peek/poke
 *
 * Parameters (all by reference; Pascal `var`):
 *   operation - word operation code, see STOP_OP_* above
 *   slot      - word slot number, 0..15 (operations 0 and 1 only)
 *   parent    - word parent slot number, negative for none (operation 1)
 *   p4        - operation 0/1: stop_$patch_rec_t * describing what to patch
 *               operation 2..7: uint32_t * holding the address to access
 *   p5        - operation 0: stop_$data_t * receiving the slot totals
 *               operation 2..7: uint32_t * holding the value (in for poke,
 *               out for peek)
 *   status    - returned status
 *
 * The two "p" parameters are overloaded by operation exactly as the
 * original code overloads (0x14,A6) and (0x18,A6), so they are untyped
 * here.
 *
 * Original address: 0x00E81814
 */
void STOP_$WATCH(int16_t *operation, uint16_t *slot, int16_t *parent, void *p4,
                 void *p5, status_$t *status);

/*
 * STOP_$WATCH_UII (0x00E81A56) and STOP_$WATCH_TRACE (0x00E81AB2) are
 * exception handlers: they save registers with `movem.l`, run at the
 * interrupt level the trap established, and end in `rte`.  They are not
 * callable from C and are emitted in stop/sau2/watch.s; no prototypes are
 * declared here.
 */

/*
 * STOP_$WATCH_TRACE_FLAG (0x00E21596)
 *
 * Set (`st`) by STOP_$WATCH_UII at 0x00E81AA6 and cleared (`sf`) by
 * STOP_$WATCH_TRACE at 0x00E81ABC.  The FIM trace-exception dispatcher reads
 * it at 0x00E215A6 to decide whether the trace trap it is handling belongs to
 * the stopwatch.  A Domain boolean: 0xFF true, tested with `< 0`.
 *
 * The cell physically sits inside the FIM data region, but it is named and
 * owned by the stopwatch; it is defined in stop/stop_data.c.
 */
extern boolean STOP_$WATCH_TRACE_FLAG;

#endif /* STOP_H */
