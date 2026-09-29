/*
 * stop/stop_internal.h - Internal definitions for the stopwatch subsystem
 *
 * The stopwatch module's data is one contiguous block that follows the code.
 * A5 is loaded at the entry point with `lea (-0xa,PC),A5` (0x00E8181C), so
 *
 *     A5 = 0x00E81814 = STOP_$WATCH
 *
 * and every (off,A5) reference in the disassembly is an offset from that.
 * The SAU2 map's STOP_WATCH segment is 0x914 bytes from 0x00E81814, so it
 * ends at 0x00E82128, where FILE_ (OS_DATA_SHUTWIRED) begins.  Its first
 * 0x3D8 bytes are code (STOP_$WATCH, STOP_$WATCH_UII, STOP_$WATCH_TRACE and
 * their helpers); the rest, A5+0x3D8 .. A5+0x914 = 0x00E81BEC .. 0x00E82128,
 * is the module's data, modelled as the single object STOP_$DATA
 * (stop_$data_t below).  The Ghidra labels of the old per-cell objects are
 * kept in the field comments:
 *
 *   +0x3D8  0x00E81BEC  saved_regs[7]    STOP_$SAVED_REGS
 *   +0x3F4  0x00E81C08  sw_overhead      STOP_$SW_OVERHEAD
 *   +0x3F8  0x00E81C0C  calibration      STOP_$CALIBRATION
 *   +0x3FC  0x00E81C10  trap_counts[65]  STOP_$TRAP_COUNTS
 *   +0x500  0x00E81D14  calib_patch      STOP_$CALIB_PATCH
 *   +0x508  0x00E81D1C  wire_start       PTR_STOP_$WATCH
 *   +0x50C  0x00E81D20  wire_end         PTR_OS_DATA_SHUTWIRED_00e81d20
 *   +0x510  0x00E81D24  wired            STOPWATCH_WIRED
 *   +0x512  0x00E81D26  wire_count       STOPWATCH_WIRE_COUNT
 *   +0x514  0x00E81D28  slots[16]        STOPWATCH_SLOTS
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
#endif

/*
 * Number of per-process trap counters: the gap between STOP_$TRAP_COUNTS
 * (0x00E81C10) and STOP_$CALIB_PATCH (0x00E81D14) is 0x104 bytes.
 */
#define STOP_TRAP_COUNT_ENTRIES 65

/*
 * ============================================================================
 * Module data (A5 = 0x00E81814): STOP_$DATA, 0x00E81BEC .. 0x00E82128
 * ============================================================================
 *
 * Field offsets below are from the start of the block; the A5 displacement
 * the code uses is always offset + STOP_DATA_A5_OFFSET.  Everything is zero
 * in the image except calib_patch, wire_start, wire_end and wire_count; the
 * stopwatch calibrates itself on the first call.
 */
#define STOP_DATA_A5         0x00E81814u /* STOP_$WATCH, `lea (-0xa,PC),A5' */
#define STOP_DATA_A5_OFFSET  0x3D8       /* STOP_DATA_ADDR - STOP_DATA_A5 */
#define STOP_DATA_SIZE       0x53C       /* to 0x00E82128 = A5 + 0x914,
                                          * the end of the map's STOP_WATCH
                                          * segment (size 0x914) */

typedef struct stop_$data_t stop_$data_t;

/* 0x00E81BEC = A5 + 0x3D8; the constant STOP_DATA_ADDR is this address. */
MODULE_DATA_DECLARE(stop_$data_t, STOP_$DATA, 0x00E81BEC);
#define STOP_DATA_ADDR       MODULE_DATA_ADDR(STOP_$DATA)

struct stop_$data_t {
    /*
     * +0x000 (A5+0x3D8), 0x00E81BEC: D0/D1/D2/A0/A1/A2/A3 as saved by
     * STOP_$WATCH_UII (`movem.l ...,(0x3d8,A2)` at 0x00E81AA0) and reloaded
     * by STOP_$WATCH_TRACE (0x00E81AB6).  Ghidra: STOP_$SAVED_REGS.
     */
    uint32_t saved_regs[7];

    /*
     * +0x01C (A5+0x3F4), 0x00E81C08.  Only the low word (0x00E81C0A) is ever
     * written, at 0x00E818F8; STOP_$WATCH_TRACE subtracts the whole longword
     * from each measured interval (0x00E81B9C, 0x00E81BC8).
     * Ghidra: STOP_$SW_OVERHEAD.
     */
    int32_t sw_overhead;

    /*
     * +0x020 (A5+0x3F8), 0x00E81C0C.  Only the low word (0x00E81C0E) is ever
     * written, at 0x00E818EC.  `tst.l (0x3f8,A5)` at 0x00E818C4 uses the
     * longword being non-zero as the "calibration already done" flag, so
     * there is no separate initialised flag anywhere in this module.
     * Ghidra: STOP_$CALIBRATION.
     */
    int32_t calibration;

    /*
     * +0x024 (A5+0x3FC), 0x00E81C10.  Indexed by PROC1_$CURRENT * 4 by the
     * trace handler (0x00E81B0A), base element 0 = pid 0; entry 0 is also
     * incremented unconditionally on every trap (`addq.l #1,(0x3fc,A2)` at
     * 0x00E81B44) and is read as the global trap count at 0x00E81B1E /
     * 0x00E81BD4.  Ghidra: STOP_$TRAP_COUNTS.
     */
    int32_t trap_counts[STOP_TRAP_COUNT_ENTRIES];

    /*
     * +0x128 (A5+0x500), 0x00E81D14: { 0x00E8193E, 0x00000000 }.  The
     * calibration hooks slot 0 onto the bare `rts` at STOP_$NULL_PROC, the
     * routine the measurement loop calls 1024 times, and leaves the exit
     * address NULL so only the entry trap fires.  Ghidra: STOP_$CALIB_PATCH.
     */
    stop_$patch_rec_t calib_patch;

    /*
     * +0x130 (A5+0x508), 0x00E81D1C: start of the region STOP_$WATCH wires
     * down, = 0x00E81814 (STOP_$WATCH itself).  Passed by reference as
     * MST_$WIRE_AREA's `start' (`pea (0x464,PC)` at 0x00E818B6, the last push).
     * Ghidra: PTR_STOP_$WATCH.
     */
    m68k_ptr_t wire_start;

    /*
     * +0x134 (A5+0x50C), 0x00E81D20: end of that region, = 0x00E82128 -- the
     * stopwatch module's own copy of the OS_DATA_SHUTWIRED start address,
     * not the one in OS_$SHUTDOWN's literal pool at 0x00E6D688
     * (os/os_data.c).  STOP_$WATCH passes this cell's address as
     * MST_$WIRE_AREA's `end' argument (`pea (0x46c,PC)` at 0x00E818B2).
     * Ghidra: PTR_OS_DATA_SHUTWIRED_00e81d20.
     */
    m68k_ptr_t wire_end;

    /* +0x138 (A5+0x510), 0x00E81D24: non-zero once MST_$WIRE_AREA has run.
     * Ghidra: STOPWATCH_WIRED. */
    int16_t wired;

    /* +0x13A (A5+0x512), 0x00E81D26: 4 in the image.
     * Ghidra: STOPWATCH_WIRE_COUNT. */
    int16_t wire_count;

    /* +0x13C (A5+0x514), 0x00E81D28 .. 0x00E82128.  Ghidra: STOPWATCH_SLOTS. */
    stopwatch_slot_t slots[STOP_MAX_SLOTS];
};

/*
 * Every field against its A5 displacement.  The fields up to and including
 * calib_patch's offset are pointer-free and hold on every build; from
 * calib_patch on the layout depends on stop_$patch_rec_t and
 * stopwatch_slot_t, which hold host pointers, so those asserts need 32-bit
 * pointers (true on the target).
 */
#define STOP_DATA_A5_OFF(field) \
    (offsetof(stop_$data_t, field) + STOP_DATA_A5_OFFSET)
_Static_assert(STOP_DATA_ADDR - STOP_DATA_A5 == STOP_DATA_A5_OFFSET,
               "STOP_$DATA base");
_Static_assert(STOP_DATA_A5_OFF(saved_regs)  == 0x3D8, "saved_regs");
_Static_assert(STOP_DATA_A5_OFF(sw_overhead) == 0x3F4, "sw_overhead");
_Static_assert(STOP_DATA_A5_OFF(calibration) == 0x3F8, "calibration");
_Static_assert(STOP_DATA_A5_OFF(trap_counts) == 0x3FC, "trap_counts");
_Static_assert(sizeof(((stop_$data_t *)0)->trap_counts) == 0x104,
               "trap_counts runs to calib_patch");
_Static_assert(STOP_DATA_A5_OFF(calib_patch) == 0x500, "calib_patch");
#if ARCH_PTR_SIZE == 4
_Static_assert(sizeof(stop_$patch_rec_t) == 8, "stop_$patch_rec_t size");
_Static_assert(STOP_DATA_A5_OFF(wire_start)  == 0x508, "wire_start");
_Static_assert(STOP_DATA_A5_OFF(wire_end)    == 0x50C, "wire_end");
_Static_assert(STOP_DATA_A5_OFF(wired)       == 0x510, "wired");
_Static_assert(STOP_DATA_A5_OFF(wire_count)  == 0x512, "wire_count");
_Static_assert(STOP_DATA_A5_OFF(slots)       == 0x514, "slots");
_Static_assert(sizeof(((stop_$data_t *)0)->slots[0]) == 0x40,
               "slot stride (lsl.w #6)");
_Static_assert(sizeof(stop_$data_t) == STOP_DATA_SIZE, "stop_$data_t size");
_Static_assert(STOP_DATA_ADDR + STOP_DATA_SIZE == 0x00E82128u,
               "STOP_$DATA ends where the STOP_WATCH segment ends");
#endif

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
