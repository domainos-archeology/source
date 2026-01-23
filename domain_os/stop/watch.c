/*
 * stop/watch.c - STOP_$WATCH implementation
 *
 * Provides stopwatch-based profiling functionality for measuring
 * execution times. Supports up to 16 concurrent stopwatch contexts.
 *
 * Original address: 0x00e81814
 *
 * This is complex Pascal-derived code with nested sub-procedures.
 * The FIM (Fault Intercept Manager) cleanup handler is used to
 * ensure consistent state even if exceptions occur during timing.
 */

#include "stop/stop.h"
#include "fim/fim.h"
#include "mst/mst.h"

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

/*
 * Global stopwatch data (at 0xe81c00 region)
 */
static struct {
    int16_t  time_scale;         /* +0x3f6 (0xe81bf6): Time scale factor */
    int16_t  reserved;           /* +0x3f8 */
    int32_t  initialized;        /* +0x3f8 (0xe81bf8): Init flag */
    int16_t  cycles_per_unit;    /* +0x3fa (0xe81bfa): Cycles per time unit */
    int16_t  pad;                /* +0x3fc */
} stopwatch_globals;

/* Stopwatch slot array (16 slots at 0xe81d28+) */
extern stopwatch_slot_t STOPWATCH_SLOTS[STOP_MAX_SLOTS];

/* Wire pointers for the stopwatch area */
extern void *PTR_STOP_$WATCH;         /* 0xe81d1c */
extern void *PTR_OS_DATA_SHUTWIRED;   /* 0xe81d20 */
extern int16_t STOPWATCH_WIRED;       /* 0xe81d24 */
extern int16_t STOPWATCH_WIRE_COUNT;  /* 0xe81d26 */

/*
 * Internal helper functions (nested Pascal sub-procedures)
 * These are translated to static functions.
 */

/* FUN_00e81916 - Read current time counter */
static int32_t read_time_counter(void);

/* FUN_00e819e2 - Release timing resources */
static void release_timing(void);

/* FUN_00e81a0a - Start timing */
static void start_timing(stopwatch_slot_t *slot, stopwatch_slot_t *parent);

/*
 * STOP_$WATCH - Start or stop a stopwatch
 *
 * Parameters:
 *   operation_p - Pointer to operation code (0=stop, 1=start)
 *   slot_p      - Pointer to slot number (0-15)
 *   parent_p    - Pointer to parent slot (-1 for none)
 *   param4      - Additional parameter
 *   data_out    - Output data (for stop operation)
 *   status_ret  - Return status
 */
void STOP_$WATCH(int16_t *operation_p, uint16_t *slot_p, int16_t *parent_p,
                 void *param4, stop_$data_t *data_out, status_$t *status_ret)
{
    status_$t status;
    int16_t operation;
    uint16_t slot_num;
    stopwatch_slot_t *slot;
    stopwatch_slot_t *parent_slot = NULL;
    uint8_t cleanup_ctx[20];
    int32_t time_start, time_end;
    uint32_t cycles_per_unit;
    int i;

    /*
     * Set up FIM cleanup handler for exception safety.
     * This ensures we release resources even if an exception occurs.
     */
    status = FIM_$CLEANUP(cleanup_ctx);
    if (status != status_$cleanup_handler_set) {
        *status_ret = status;
        return;
    }

    operation = *operation_p;

    /*
     * Handle operation codes > 1 via jump table.
     * Codes 0 and 1 are the main start/stop operations.
     */
    if (operation > 1) {
        /*
         * Higher operation codes are dispatched through a jump table.
         * This appears to be used for extended operations like trace mode.
         * TODO: Implement jump table dispatch for operations 2+
         */
        FIM_$RLS_CLEANUP(cleanup_ctx);
        *status_ret = status_$ok;
        return;
    }

    /*
     * Ensure the stopwatch area is wired into memory.
     * This prevents page faults during timing operations.
     */
    if (STOPWATCH_WIRED == 0) {
        uint8_t wire_ctx[16];
        MST_$WIRE_AREA(&PTR_STOP_$WATCH, &PTR_OS_DATA_SHUTWIRED,
                       wire_ctx, &STOPWATCH_WIRE_COUNT, &STOPWATCH_WIRED);
    }

    /*
     * One-time initialization: calibrate the timer.
     * Measures the cycles per time unit for accurate scaling.
     */
    if (stopwatch_globals.initialized == 0) {
        time_start = read_time_counter();
        start_timing(NULL, NULL);
        time_end = read_time_counter();

        /* Calculate cycles per unit (divide by 2048) */
        stopwatch_globals.cycles_per_unit = (int16_t)((time_end - time_start) / 0x800);

        /* TODO: Get additional timing info from system */
        /* stopwatch_globals.time_scale = ... */

        release_timing();
        stopwatch_globals.initialized = 1;
    }

    /* Validate slot number */
    slot_num = *slot_p;
    if (slot_num >= STOP_MAX_SLOTS) {
        status = 0x300001;  /* status_$audit_invalid_data_size */
        goto cleanup;
    }

    /* Get pointer to the slot */
    slot = &STOPWATCH_SLOTS[slot_num];

    if (operation >= 1) {
        /* START operation */

        /* Check if already active */
        if ((slot->flags & 0x80) != 0) {
            status = 0x300002;  /* status_$audit_file_already_open */
            goto cleanup;
        }

        /* Set up parent slot if specified */
        if (*parent_p >= 0 && *parent_p < STOP_MAX_SLOTS) {
            parent_slot = &STOPWATCH_SLOTS[*parent_p];
        }

        /* Start timing */
        start_timing(slot, parent_slot);
        status = status_$ok;

    } else {
        /* STOP operation (operation == 0) */

        /* Check if active */
        if ((slot->flags & 0x80) == 0) {
            status = 0x300001;  /* Not running */
            goto cleanup;
        }

        cycles_per_unit = stopwatch_globals.cycles_per_unit;

        /*
         * Calculate elapsed time, adjusting for accumulated counts.
         * The timing data is stored in the slot structure and copied
         * to the output buffer.
         */

        /* Clear accumulator counts */
        slot->count1 = 0;
        slot->count3 = 0;

        /* Subtract calibration overhead */
        slot->time1_low -= cycles_per_unit * slot->count2;
        slot->time2_low -= cycles_per_unit * slot->count4;

        /* Copy timing data to output */
        for (i = 0; i < 4; i++) {
            ((int32_t *)data_out)[i] = ((int32_t *)&slot->time1_high)[i];
            ((int32_t *)&slot->time1_high)[i] = 0;
        }

        /* Release timing if needed */
        if (operation != 0) {
            release_timing();
        }

        status = status_$ok;
    }

cleanup:
    FIM_$RLS_CLEANUP(cleanup_ctx);
    *status_ret = status;
}

/*
 * read_time_counter - Read current high-resolution time counter
 *
 * Returns the current value of the system's high-resolution timer.
 * This is used for calculating elapsed time in stopwatch operations.
 *
 * Original address: 0x00e81916
 */
static int32_t read_time_counter(void)
{
    /*
     * TODO: Implement actual timer read.
     * On m68k, this typically reads from a hardware timer register
     * or the system's microsecond clock.
     */
    return 0;
}

/*
 * release_timing - Release timing resources
 *
 * Called when stopping a stopwatch to release any system resources
 * that were acquired for timing.
 *
 * Original address: 0x00e819e2
 */
static void release_timing(void)
{
    /*
     * TODO: Implement resource release.
     * May involve releasing hardware timer access or
     * decrementing reference counts.
     */
}

/*
 * start_timing - Start timing for a stopwatch slot
 *
 * Initializes the slot for timing and optionally links to a parent
 * slot for hierarchical profiling.
 *
 * Parameters:
 *   slot   - Stopwatch slot to start
 *   parent - Parent slot for hierarchical timing (can be NULL)
 *
 * Original address: 0x00e81a0a
 */
static void start_timing(stopwatch_slot_t *slot, stopwatch_slot_t *parent)
{
    (void)parent;  /* Unused for now */

    if (slot != NULL) {
        /* Mark slot as active */
        slot->flags |= 0x80;

        /* Initialize timing accumulators */
        slot->time1_high = 0;
        slot->time1_low = 0;
        slot->time2_high = 0;
        slot->time2_low = 0;
        slot->count1 = 0;
        slot->count2 = 0;
        slot->count3 = 0;
        slot->count4 = 0;
    }

    /*
     * TODO: Set up hardware timer if needed.
     * May involve acquiring exclusive access to a timer channel.
     */
}
