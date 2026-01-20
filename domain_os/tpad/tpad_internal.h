/*
 * tpad/tpad_internal.h - Trackpad Module Internal API
 *
 * Internal header for TPAD subsystem implementation.
 * Defines global state variables and internal helper functions.
 */

#ifndef TPAD_INTERNAL_H
#define TPAD_INTERNAL_H

#include "tpad/tpad.h"
#include "smd/smd.h"
#include "time/time.h"
#include "math/math.h"

/*
 * ============================================================================
 * Global State Variables
 * ============================================================================
 * These globals mirror the original data layout at 0x00E8245C.
 */

/* Per-unit configuration array (max 8 units) */
extern tpad_$unit_config_t tpad_$unit_configs[TPAD_$MAX_UNITS];

/* Global TPAD state */
extern tpad_$globals_t tpad_$globals;

/*
 * Convenience accessors for global state fields.
 * These match the original code's naming conventions.
 */
#define tpad_$unit          tpad_$globals.unit
#define tpad_$dev_type      tpad_$globals.dev_type
#define tpad_$cursor_y      tpad_$globals.cursor_y
#define tpad_$cursor_x      tpad_$globals.cursor_x
#define tpad_$raw_y         tpad_$globals.raw_y
#define tpad_$raw_x         tpad_$globals.raw_x
#define tpad_$button_state  tpad_$globals.button_state
#define tpad_$delta_y       tpad_$globals.delta_y
#define tpad_$delta_x       tpad_$globals.delta_x
#define tpad_$accum_y       tpad_$globals.accum_y
#define tpad_$accum_x       tpad_$globals.accum_x
#define tpad_$touchpad_max  tpad_$globals.touchpad_max
#define tpad_$last_clock    tpad_$globals.last_clock
#define tpad_$re_origin_flag tpad_$globals.re_origin_flag

/* Unit number used during initialization */
extern int16_t tpad_$unit_num_for_init;

/*
 * ============================================================================
 * Internal Helper Macros
 * ============================================================================
 */

/* Get pointer to unit configuration (1-indexed) */
#define TPAD_$UNIT_CONFIG(unit) (&tpad_$unit_configs[(unit) - 1])

/* Validate unit number (returns true if valid) */
#define TPAD_$VALID_UNIT(unit) ((unit) > 0 && (unit) <= SMD_$N_DEVICES())

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */
#define status_$display_invalid_unit_number  ((status_$t){0x00130001})

#endif /* TPAD_INTERNAL_H */
