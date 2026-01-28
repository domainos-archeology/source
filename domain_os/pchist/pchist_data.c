/*
 * PCHIST data definitions
 *
 * Global variables for the PC histogram (profiling) subsystem.
 * These are declared extern in pchist.h and pchist_internal.h.
 */

#include "pchist/pchist_internal.h"

/*
 * Main control structure for the PCHIST subsystem
 * Contains the exclusion lock, process bitmap, per-process PC storage,
 * and profiling state flags.
 * Located at 0xe2c204
 *
 * Note: PCHIST_$PROC_BITMAP and PCHIST_$PROC_PC are fields within
 * this structure (at offsets 0x18 and 0x1C respectively), accessed
 * via macros defined in pchist_internal.h.
 */
pchist_control_t PCHIST_$CONTROL;

/*
 * Per-process profiling data array
 * Indexed by process ID, each entry holds the profil() parameters
 * (buffer address, size, offset, scale, overflow pointer).
 * Located at 0xe85704
 */
pchist_proc_t PCHIST_$PROC_DATA[PCHIST_MAX_PROCESSES];

/*
 * Wire page tracking array for histogram buffer
 * Stores page addresses that have been wired into memory
 * during active profiling.
 * Located at 0xe85c14
 */
uint32_t PCHIST_$WIRE_PAGES[4]; /* TODO: verify array size */

/*
 * MST wire area context pointer
 * Used during wiring/unwiring of histogram buffer pages.
 * Located at 0xe85c18
 */
void *PCHIST_$WIRE_CONTEXT;

/*
 * System-wide histogram data structure
 * Contains histogram parameters, counters, and bin array.
 * Located at 0xe85c24
 */
pchist_histogram_t PCHIST_$HISTOGRAM;

/*
 * Count of currently wired pages
 * Located at 0xe8604e
 */
int16_t PCHIST_$WIRED_COUNT;
