/*
 * stop/stop_internal.h - Internal definitions for the stopwatch subsystem
 *
 * The stopwatch module's data is one contiguous block that follows the code.
 * A5 is loaded at the entry point with `lea (-0xa,PC),A5` (0x00E8181C), so
 *
 *     A5 = 0x00E81814 = STOP_$WATCH
 *
 * and every (off,A5) reference in the disassembly is an offset from that.
 * The offsets used by the module are:
 *
 *   +0x3D8  0x00E81BEC  STOP_$SAVED_REGS[7]   register image handed from
 *                                             STOP_$WATCH_UII to
 *                                             STOP_$WATCH_TRACE
 *   +0x3F4  0x00E81C08  STOP_$SW_OVERHEAD     long; its low word (0x00E81C0A)
 *                                             is the measured per-event cost
 *                                             in stopwatch-accumulated units
 *   +0x3F8  0x00E81C0C  STOP_$CALIBRATION     long; its low word (0x00E81C0E)
 *                                             is the measured per-event cost
 *                                             in PROC1_$GET_CPUT units.  The
 *                                             whole long doubles as the
 *                                             "already calibrated" flag
 *                                             (`tst.l (0x3f8,A5)`, 0x00E818C4)
 *   +0x3FC  0x00E81C10  STOP_$TRAP_COUNTS[65] per-process trap counters,
 *                                             indexed by PROC1_$CURRENT;
 *                                             entry 0 doubles as the global
 *                                             trap counter
 *   +0x500  0x00E81D14  STOP_$CALIB_PATCH     patch record used to calibrate:
 *                                             { &STOP_$NULL_PROC, NULL }
 *   +0x508  0x00E81D1C  PTR_STOP_$WATCH       = 0x00E81814
 *   +0x50C  0x00E81D20  PTR_OS_DATA_SHUTWIRED_00e81d20 = 0x00E82128
 *   +0x510  0x00E81D24  STOPWATCH_WIRED       word
 *   +0x512  0x00E81D26  STOPWATCH_WIRE_COUNT  word, = 4 in the image
 *   +0x514  0x00E81D28  STOPWATCH_SLOTS[16]   64 bytes each
 *
 * External consumers should use stop/stop.h.
 */

#ifndef STOP_INTERNAL_H
#define STOP_INTERNAL_H

#include "stop/stop.h"
#include "os/os.h"     /* OS_$SHUTDOWN, os status codes */
#include "disk/disk.h" /* DISK_$DIAG */

/*
 * Stopwatch slot (64 bytes; the slot index scale is `lsl.w #6`).
 *
 * Field offsets recovered from STOP_$WATCH (0x00E81998-0x00E819D8), the
 * hook helper (0x00E81A0A) and the trace handler (0x00E81AB2).
 */
typedef struct stopwatch_slot_t {
    uint16_t *patch1;    /* +0x00: entry patch address (0x00E81A22) */
    uint16_t *patch2;    /* +0x04: exit patch address, NULL if none */
    struct stopwatch_slot_t *parent; /* +0x08: enclosing slot (0x00E81A14) */
    uint16_t saved1;     /* +0x0C: original word at *patch1 */
    uint16_t saved2;     /* +0x0E: original word at *patch2 */
    uint8_t flags;       /* +0x10: bit7 running, bit6 has patch2,
                          *        bit5 interval in progress */
    uint8_t pad11;       /* +0x11 */
    int16_t owner;       /* +0x12: PROC1_$CURRENT that opened the interval */
    int32_t completions; /* +0x14: intervals completed */
    int32_t reentries;   /* +0x18: entry traps while already in an interval */
    int32_t cpu_time;    /* +0x1C: accumulated PROC1_$GET_CPUT delta */
    int32_t elapsed_time;/* +0x20: accumulated TIME_$CLOCK delta */
    int32_t cpu_events;  /* +0x24: traps charged against cpu_time */
    int32_t elapsed_events; /* +0x28: traps charged against elapsed_time */
    int32_t entry_cput;  /* +0x2C: PROC1_$GET_CPUT at interval start */
    int32_t entry_clock; /* +0x30: TIME_$CLOCK at interval start */
    int32_t entry_traps; /* +0x34: this process's trap count at start */
    int32_t entry_gtraps;/* +0x38: global trap count at start */
    int32_t reserved3c;  /* +0x3C */
} stopwatch_slot_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(stopwatch_slot_t, pad11) == 0x11, "stopwatch_slot_t.pad11");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, reserved3c) == 0x3C, "stopwatch_slot_t.reserved3c");
#endif

/*
 * Slot flag bits.  The code uses `btst.b #n,(0x10,A1)` on the byte at +0x10,
 * so these are bit numbers within that byte, not within a word.
 */
#define STOP_SLOT_RUNNING 0x80    /* bit 7 (0x00E8194E, 0x00E81A46) */
#define STOP_SLOT_HAS_EXIT 0x40   /* bit 6 (0x00E81A36) */
#define STOP_SLOT_IN_INTERVAL 0x20 /* bit 5 (0x00E81AEC) */

#if defined(ARCH_M68K)
_Static_assert(sizeof(stopwatch_slot_t) == 0x40, "stopwatch_slot_t size");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, patch1) == 0x00, "patch1");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, patch2) == 0x04, "patch2");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, parent) == 0x08, "parent");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, saved1) == 0x0C, "saved1");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, saved2) == 0x0E, "saved2");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, flags) == 0x10, "flags");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, owner) == 0x12, "owner");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, completions) == 0x14, "c");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, reentries) == 0x18, "r");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, cpu_time) == 0x1C, "cpu");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, elapsed_time) == 0x20, "e");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, cpu_events) == 0x24, "ce");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, elapsed_events) == 0x28,
               "ee");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, entry_cput) == 0x2C, "ec");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, entry_clock) == 0x30, "el");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, entry_traps) == 0x34, "et");
_Static_assert(__builtin_offsetof(stopwatch_slot_t, entry_gtraps) == 0x38,
               "eg");
_Static_assert(sizeof(stop_$data_t) == 0x10, "stop_$data_t size");
#endif

/*
 * Number of per-process trap counters: the gap between STOP_$TRAP_COUNTS
 * (0x00E81C10) and STOP_$CALIB_PATCH (0x00E81D14) is 0x104 bytes.
 */
#define STOP_TRAP_COUNT_ENTRIES 65

/*
 * ============================================================================
 * Module data (A5 = 0x00E81814)
 * ============================================================================
 */
extern uint32_t STOP_$SAVED_REGS[7];   /* 0x00E81BEC */
extern int32_t STOP_$SW_OVERHEAD;      /* 0x00E81C08 */
extern int32_t STOP_$CALIBRATION;      /* 0x00E81C0C */
extern int32_t STOP_$TRAP_COUNTS[STOP_TRAP_COUNT_ENTRIES]; /* 0x00E81C10 */
extern stop_$patch_rec_t STOP_$CALIB_PATCH; /* 0x00E81D14 */
extern m68k_ptr_t PTR_STOP_$WATCH;     /* 0x00E81D1C */
/* 0x00E81D20 (A5+0x50C): the end address of the region STOP_$WATCH wires. */
extern m68k_ptr_t PTR_OS_DATA_SHUTWIRED_00e81d20;
extern int16_t STOPWATCH_WIRED;        /* 0x00E81D24 */
extern int16_t STOPWATCH_WIRE_COUNT;   /* 0x00E81D26 */
extern stopwatch_slot_t STOPWATCH_SLOTS[STOP_MAX_SLOTS]; /* 0x00E81D28 */

/*
 * DISK_$DIAG (0x00E7ACCA, declared in disk/disk.h) gates the poke
 * operations.  STOP_$WATCH tests it with `tst.b / bne` at 0x00E8186A --
 * non-zero means diagnostics are enabled, so this is not the usual
 * 0xFF-boolean `< 0` test that DISK_$DIAG_IO uses.
 */

/*
 * ============================================================================
 * Hand-written assembly helpers (stop/sau2/watch.s)
 * ============================================================================
 *
 * All four take their arguments in registers and none of them follows the
 * Pascal stack convention, so the .s file provides a thin C-callable wrapper
 * around each; the wrapper name is what appears below and the original
 * register-argument entry point keeps the Ghidra name given in the comment.
 */

/*
 * STOP_$NULL_PROC (0x00E8193E) - a bare `rts`.
 *
 * It is both the tail of STOP_$MEASURE_LOOP (which has no `rts` of its own
 * and falls through into it) and the routine the measurement loop calls, so
 * its address is what STOP_$CALIB_PATCH patches.
 */
void STOP_$NULL_PROC(void);

/*
 * stop_$measure_loop - STOP_$MEASURE_LOOP (0x00E81916)
 *
 * Runs 1024 iterations of {bsr to a bare rts, jsr CACHE_$CLEAR} with
 * interrupts masked and returns the PROC1_$GET_CPUT delta over the whole
 * loop.  Note the routine has no `rts` of its own: it falls through into
 * STOP_$NULL_PROC (0x00E8193E), whose `rts` returns for it.  It exits
 * through `andi #-0x701,SR`, which forces IPL 0 rather than restoring the
 * caller's level.
 */
int32_t stop_$measure_loop(void);

/*
 * stop_$hook - STOP_$HOOK (0x00E81A0A)
 *
 * Register arguments: A0 = patch record, A1 = slot, A2 = parent slot (or
 * NULL), D0.w = slot number (the wrapper takes it as a longword and hands
 * the low word to D0).  Zeroes the whole 64-byte slot, patches
 * 0xA000+slotno over *patch1 (saving the original), and if patch2 is
 * non-NULL patches 0xA100+slotno over it too and sets flag bit 6.  Sets
 * flag bit 7 and calls CACHE_$CLEAR.  Runs under `move SR,D1 / ori
 * #0x700,SR` ... `move D1,SR`.
 */
void stop_$hook(const stop_$patch_rec_t *rec, stopwatch_slot_t *slot,
                stopwatch_slot_t *parent, uint32_t slotno);

/*
 * stop_$unhook - STOP_$UNHOOK (0x00E819E2)
 *
 * Register argument: A1 = slot.  Restores the saved instruction words,
 * calls CACHE_$CLEAR and clears flag bit 7, under a true SR save/restore.
 */
void stop_$unhook(stopwatch_slot_t *slot);

#endif /* STOP_INTERNAL_H */
