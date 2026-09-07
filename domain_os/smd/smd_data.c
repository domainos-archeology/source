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
 *   0x00E27376 - SMD_DISPLAY_INFO array
 *   0x00E273D6 - SMD_BLINK_STATE
 *   0x00E84924 - SMD_DEFAULT_DISPLAY_UNIT
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
 * Display info table - configuration for each display
 * Original address: 0x00E27376
 */
smd_display_info_t SMD_DISPLAY_INFO[SMD_MAX_DISPLAY_UNITS];

/*
 * Primary SMD event count (same address as display unit 0)
 * Original address: 0x00E2E3FC
 */
ec_$eventcount_t SMD_EC_1;

/*
 * Secondary SMD event count
 * Original address: 0x00E2E408
 */
ec_$eventcount_t SMD_EC_2;

/*
 * Cursor blink state
 * Original address: 0x00E273D6
 */
smd_blink_state_t SMD_BLINK_STATE;

/*
 * Display unit record initialisers, original address 0x00E173D4.
 * Contents (read with gsk): 00 00 04 00 00 00 00 00.
 */
const uint32_t smd_$unit_init_params[2] = { 0x00000400u, 0x00000000u };

/*
 * Default display unit number (stored separately from globals)
 * Original address: 0x00E84924
 */
uint16_t SMD_DEFAULT_DISPLAY_UNIT;

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
