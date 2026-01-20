/*
 * tpad/tpad_data.c - TPAD subsystem global data
 *
 * Contains the global state variables for the pointing device subsystem.
 *
 * Original data area: 0x00E8245C - 0x00E825DF
 */

#include "tpad/tpad_internal.h"

/*
 * Per-unit configuration array.
 * Each entry is 44 bytes (0x2c), supporting up to 8 display units.
 * Original location: 0x00E8245C
 */
tpad_$unit_config_t tpad_$unit_configs[TPAD_$MAX_UNITS];

/*
 * Global TPAD state.
 * Original location: 0x00E825BC (offset 0x160 from base)
 */
tpad_$globals_t tpad_$globals;

/*
 * Unit number used during initialization.
 * Original location: relative to 0x00E33570 (TPAD_$INIT)
 */
int16_t tpad_$unit_num_for_init = 1;
