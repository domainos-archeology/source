/*
 * stop/stop_internal.h - Internal definitions for the stopwatch subsystem
 *
 * Contains the stopwatch slot layout and the globals used only within
 * stop/.  External consumers should use stop/stop.h.
 */

#ifndef STOP_INTERNAL_H
#define STOP_INTERNAL_H

#include "stop/stop.h"
#include "os/os.h"      /* PTR_OS_DATA_SHUTWIRED */

/*
 * Stopwatch slot structure (64 bytes per slot)
 *
 * Each slot tracks timing data for one profiling context.
 */
typedef struct {
    int32_t reserved1[4];        /* +0x00: Reserved */
    uint8_t flags;               /* +0x10: Flags (bit 7 = active) */
    uint8_t pad1[3];             /* Padding */
    int32_t time1_high;          /* +0x14: Time accumulator 1 high */
    int32_t time1_low;           /* +0x18: Time accumulator 1 low */
    int32_t time2_high;          /* +0x1c: Time accumulator 2 high */
    int32_t time2_low;           /* +0x20: Time accumulator 2 low */
    int16_t count1;              /* +0x24: Count 1 */
    int16_t count2;              /* +0x26: Count 2 */
    int16_t count3;              /* +0x28: Count 3 */
    int16_t count4;              /* +0x2a: Count 4 */
    int32_t reserved2[5];        /* +0x2c: Reserved to 0x40 */
} stopwatch_slot_t;

/* Stopwatch slot array (16 slots at 0xE81D28+) */
extern stopwatch_slot_t STOPWATCH_SLOTS[STOP_MAX_SLOTS];

/*
 * Wire descriptor cells for the stopwatch area.
 *   0x00E81D1C: PTR_STOP_$WATCH      - start of the area to wire
 *   0x00E81D20: PTR_OS_DATA_SHUTWIRED - end of the area (declared in os/os.h)
 *   0x00E81D24: STOPWATCH_WIRED      - non-zero once wired
 *   0x00E81D26: STOPWATCH_WIRE_COUNT - wire count returned by MST_$WIRE_AREA
 */
extern m68k_ptr_t PTR_STOP_$WATCH;
extern int16_t STOPWATCH_WIRED;
extern int16_t STOPWATCH_WIRE_COUNT;

#endif /* STOP_INTERNAL_H */
