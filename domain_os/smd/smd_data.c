/*
 * smd/smd_data.c - SMD global data definitions
 *
 * This file defines the global data structures used by the SMD subsystem.
 * In the original binary these are at fixed addresses in memory.
 *
 * Memory layout (m68k):
 *   0x00E82B8C - SMD_GLOBALS
 *   0x00E2E3FC - SMD_DISPLAY_UNITS array (also SMD_EC_1)
 *   0x00E2E408 - SMD_EC_2
 *   0x00E27376 - SMD_DISPLAY_INFO array (SMD_$DISPLAY_COM, ONE entry)
 *   0x00E273D6 - SMD_TIME_$COM (6 bytes; MNK_$KTT_PTRS follows at 0x00E273DC)
 *   0x00E84924 - SMD_GLOBALS.default_unit (SMD_GLOBALS + 0x1D98)
 */

#include "smd/smd_internal.h"

/*
 * SMD global state structure
 * Original address: 0x00E82B8C
 */
smd_globals_t SMD_GLOBALS;

/*
 * Display unit area - the two standalone eventcounts followed by the per-unit
 * display records.  See smd_internal.h: record N lives at
 * SMD_DISPLAY_UNITS + N*0x10C - 0xF4, which is what smd_$unit_rec() computes.
 * Original address: 0x00E2E3FC
 */
uint8_t SMD_DISPLAY_UNITS[SMD_MAX_DISPLAY_UNITS * SMD_DISPLAY_UNIT_SIZE + 0x18];

/*
 * Display info / hardware record table.
 * Original address: 0x00E27376 (Ghidra label SMD_$DISPLAY_COM).
 *
 * Exactly SMD_DISPLAY_INFO_COUNT (= 1) entry: the single 0x60-byte record
 * runs to 0x00E273D5 and SMD_TIME_$COM starts at 0x00E273D6.  This used to be
 * sized SMD_MAX_DISPLAY_UNITS, which over-allocated 0x120 bytes (source-9j2l).
 */
smd_display_info_t SMD_DISPLAY_INFO[SMD_DISPLAY_INFO_COUNT];

/*
 * SMD_EC_1 (0x00E2E3FC) and SMD_EC_2 (0x00E2E408) are not separate objects:
 * they are the first 0x18 bytes of SMD_DISPLAY_UNITS above.  smd_internal.h
 * defines them as aliases into that block (bead source-ufwn), so there is
 * nothing to define here.
 */

/*
 * SMD_TIME_$COM - the SMD_TIME module's common block (cursor blink state).
 * Original address: 0x00E273D6, 6 bytes.  It is the object that immediately
 * follows SMD_DISPLAY_INFO's single entry, which is how that table's length
 * is pinned down.
 */
smd_time_com_t SMD_TIME_$COM;

/*
 * Display unit record initialisers, original address 0x00E173D4.
 * Contents (read with gsk): 00 00 04 00 00 00 00 00.
 */
const uint32_t smd_$unit_init_params[2] = { 0x00000400u, 0x00000000u };

/*
 * There is no separate "default display unit" object: 0x00E84924 is
 * SMD_GLOBALS + 0x1D98 (0x00E82B8C + 0x1D98), i.e. SMD_GLOBALS.default_unit.
 * See the field comment in smd_internal.h (bead source-nuan).
 */

/* Request queue event counts */
ec_$eventcount_t SMD_REQUEST_EC_WAIT;  /* At 0x00E2E3FC - wait for space */
ec_$eventcount_t SMD_REQUEST_EC_SIGNAL; /* At 0x00E2E408 - signal new request */

/*
 * Constant lock/line words that live in the code segment of the original
 * binary.  Their addresses are passed to SMD_$ACQ_DISPLAY, KBD_$*, and
 * TERM_$CONTROL as by-reference arguments.
 *   0x00E6D92A: 0x0001
 *   0x00E6D92C: 0x0000
 *   0x00E6DFF8: 0x0001
 */
uint16_t SMD_ACQ_LOCK_DATA = 0;
int16_t SMD_SYNC_LOCK_DATA = 1;

/*
 * A second constant word holding 0x0001, at 0x00E6D92A.
 */
int16_t SMD_ONE_LOCK_DATA = 1;

/*
 * Three more code-region constant cells (bead source-2c9v).  Values read with
 * `gsk read`:
 *   0x00E6E59A  ff ff  -> the word -1
 *   0x00E6E458  ff     -> the Domain boolean true
 *   0x00E6E45A  00     -> the Domain boolean false
 * They live in the read-only code segment of the original, hence `const`.
 */
const int16_t SMD_MINUS_ONE_DATA = -1;
const boolean SMD_TRUE_DATA = true;
const boolean SMD_FALSE_DATA = false;
