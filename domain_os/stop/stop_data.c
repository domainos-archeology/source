/*
 * stop/stop_data.c - Stopwatch module data (A5 = 0x00E81814)
 *
 * These are the real module globals, at the (off,A5) displacements listed in
 * stop/stop_internal.h -- not a private copy.  Everything except
 * STOP_$CALIB_PATCH, PTR_STOP_$WATCH and STOPWATCH_WIRE_COUNT is zero in the
 * image; the stopwatch calibrates itself on the first call.
 */

#include "stop/stop_internal.h"

/* 0x00E81BEC (A5+0x3D8): D0/D1/D2/A0/A1/A2/A3 as saved by STOP_$WATCH_UII */
uint32_t STOP_$SAVED_REGS[7];

/*
 * 0x00E81C08 (A5+0x3F4).  Only the low word (0x00E81C0A) is ever written,
 * at 0x00E818F8; STOP_$WATCH_TRACE subtracts the whole longword from each
 * measured interval (0x00E81B9C, 0x00E81BC8).
 */
int32_t STOP_$SW_OVERHEAD;

/*
 * 0x00E81C0C (A5+0x3F8).  Only the low word (0x00E81C0E) is ever written,
 * at 0x00E818EC.  `tst.l (0x3f8,A5)` at 0x00E818C4 uses the longword being
 * non-zero as the "calibration already done" flag, so there is no separate
 * initialised flag anywhere in this module.
 */
int32_t STOP_$CALIBRATION;

/*
 * 0x00E81C10 (A5+0x3FC).  Indexed by PROC1_$CURRENT * 4 by the trace
 * handler (0x00E81B0A); entry 0 is also incremented unconditionally on
 * every trap (`addq.l #1,(0x3fc,A2)` at 0x00E81B44) and is read as the
 * global trap count at 0x00E81B1E / 0x00E81BD4.
 */
int32_t STOP_$TRAP_COUNTS[STOP_TRAP_COUNT_ENTRIES];

/*
 * 0x00E81D14 (A5+0x500): { 0x00E8193E, 0x00000000 }.
 *
 * The calibration hooks slot 0 onto the bare `rts` at STOP_$NULL_PROC, the
 * routine the measurement loop calls 1024 times, and leaves the exit
 * address NULL so only the entry trap fires.
 */
stop_$patch_rec_t STOP_$CALIB_PATCH = {
    /* .entry_addr = */ (uint16_t *)STOP_$NULL_PROC, /* 0x00E8193E */
    /* .exit_addr  = */ NULL,
};

/* 0x00E81D1C (A5+0x508): start of the region STOP_$WATCH wires down */
m68k_ptr_t PTR_STOP_$WATCH = 0x00E81814;

/* 0x00E81D24 (A5+0x510): non-zero once MST_$WIRE_AREA has run */
int16_t STOPWATCH_WIRED;

/* 0x00E81D26 (A5+0x512): 4 in the image */
int16_t STOPWATCH_WIRE_COUNT = 4;

/* 0x00E81D28 (A5+0x514) .. 0x00E82128 */
stopwatch_slot_t STOPWATCH_SLOTS[STOP_MAX_SLOTS];

/*
 * 0x00E21596.  Physically inside the FIM data region, but written only by
 * STOP_$WATCH_UII / STOP_$WATCH_TRACE (stop/sau2/watch.s) and read by the
 * FIM trace dispatcher at 0x00E215A6.
 */
boolean STOP_$WATCH_TRACE_FLAG;
