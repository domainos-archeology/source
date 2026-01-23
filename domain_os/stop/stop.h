/*
 * stop/stop.h - Stopwatch Profiling Subsystem
 *
 * This module provides stopwatch-based profiling functionality for
 * measuring execution times and collecting performance metrics.
 * It supports multiple simultaneous stopwatch contexts.
 *
 * Up to 16 stopwatch slots are available, each tracking:
 *   - Elapsed time
 *   - Count of start/stop cycles
 *   - Optional trace mode logging
 */

#ifndef STOP_H
#define STOP_H

#include "base/base.h"

/*
 * Maximum number of stopwatch slots.
 */
#define STOP_MAX_SLOTS  16

/*
 * Stopwatch data structure returned by STOP_$WATCH
 *
 * Contains the accumulated timing data when a stopwatch is stopped.
 */
typedef struct {
    int32_t time_high;           /* High 32 bits of elapsed time */
    int32_t time_low;            /* Low 32 bits of elapsed time */
    int32_t count_high;          /* High 32 bits of iteration count */
    int32_t count_low;           /* Low 32 bits of iteration count */
} stop_$data_t;

/*
 * STOP_$WATCH - Start or stop a stopwatch
 *
 * This function controls stopwatch timers for profiling purposes.
 * It can start a new timing session or stop an existing one and
 * retrieve the accumulated data.
 *
 * Parameters:
 *   operation - Operation code:
 *               0 = Stop the stopwatch and return data
 *               1 = Start the stopwatch
 *               2+ = Jump table for other operations
 *   slot      - Stopwatch slot number (0-15)
 *   parent    - Parent stopwatch slot (-1 for none)
 *   param4    - Additional parameter (operation-specific)
 *   data_out  - Pointer to receive stopwatch data (for stop operation)
 *   status    - Pointer to receive status code
 *
 * Status codes:
 *   status_$ok - Operation completed successfully
 *   status_$audit_invalid_data_size (0x300001) - Invalid slot number
 *   status_$audit_file_already_open (0x300002) - Stopwatch already in use
 *
 * The timing resolution depends on the system timer, typically
 * around 2KB (2048) cycles per unit for the main timer.
 *
 * Original address: 0x00e81814
 */
void STOP_$WATCH(int16_t *operation, uint16_t *slot, int16_t *parent,
                 void *param4, stop_$data_t *data_out, status_$t *status);

/*
 * STOP_$WATCH_UII - Stopwatch with UID parameter
 *
 * Extended version of STOP_$WATCH that accepts a UID parameter
 * for additional context.
 *
 * Original address: 0x00e81a56
 */
void STOP_$WATCH_UII(int16_t *operation, uint16_t *slot, int16_t *parent,
                     void *param4, stop_$data_t *data_out, status_$t *status);

/*
 * STOP_$WATCH_TRACE - Stopwatch with trace mode
 *
 * Extended version of STOP_$WATCH that enables trace logging
 * of all start/stop events.
 *
 * Original address: 0x00e81ab2
 */
void STOP_$WATCH_TRACE(int16_t *operation, uint16_t *slot, int16_t *parent,
                       void *param4, stop_$data_t *data_out, status_$t *status);

#endif /* STOP_H */
