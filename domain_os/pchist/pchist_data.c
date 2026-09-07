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
 * Wire page tracking array for the histogram buffer.
 *
 * The list is ONE-BASED, and the declaration carries the bias: the base is
 * 0xE85C14 but slot 0 is not part of the list.  Both users agree on that:
 *
 *   PCHIST_$CNTL   passes 0xE85C18 to MST_$WIRE_AREA (0x00E5CF00), which
 *                  stores with "move.l D0,(-0x4,A2,D1w*1)", D1 = count*4,
 *                  i.e. at 0xE85C18 + (count-1)*4.
 *   PCHIST_$UNWIRE_CLEANUP indexes from 0xE85718 + 0x4FC = 0xE85C14 with
 *                  i running 1..count (0x00E5CD42).
 *
 * The size is pinned by the page limit MST_$WIRE_AREA is given, the constant
 * 3 at 0x00E5CFAA: slots 1..3 (0xE85C18..0xE85C23), which ends exactly at
 * PCHIST_$HISTOGRAM (0xE85C24).  Three is enough because the wired area is
 * the 0x428-byte histogram record, spanning at most three 1KB pages.
 *
 * Located at 0xe85c14 (slot 0 unused)
 */
uint32_t PCHIST_$WIRE_PAGES[4];

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
