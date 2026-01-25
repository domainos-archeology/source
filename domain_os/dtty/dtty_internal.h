/*
 * dtty/dtty_internal.h - Display TTY Module Internal API
 *
 * Internal functions and data used within the DTTY subsystem.
 */

#ifndef DTTY_INTERNAL_H
#define DTTY_INTERNAL_H

#include "dtty/dtty.h"
#include "smd/smd_internal.h" // SMD_$COPY_FONT_DO_HDM is here... not sure why
#include "proc1/proc1.h"

/*
 * ============================================================================
 * Internal Global Data
 * ============================================================================
 *
 * DTTY uses a small data block starting at 0x00E2E00C:
 *   0x00E2E00C: DTTY_$DISP_TYPE   (2 bytes) - Display type (1 or 2)
 *   0x00E2E00E: DTTY_$CTRL        (2 bytes) - Control word
 *   0x00E2E010: field_04          (1 byte)  - Unknown flag
 *   0x00E2E011: (padding?)        (1 byte)
 *   0x00E2E012: field_06          (1 byte)  - Internal flag (set to 0xFF)
 *   0x00E2E013: (padding?)        (1 byte)
 *   0x00E2E014: DTTY_$USE_DTTY    (1 byte)  - Use DTTY flag
 */

/*
 * Hardware-specific display status registers
 *
 * These memory locations contain display status bits.
 * Bit 0 indicates display hardware presence.
 */
#define DISP_15_STATUS_ADDR     0x00FC0066  /* 15" display status */
#define DISP_19_STATUS_ADDR     0x00FDEBE6  /* 19" display status */

/*
 * Display unit number for SMD association
 */
#define DTTY_DISPLAY_UNIT       1

/*
 * SMD status codes used by DTTY
 */
#define status_$display_unsupported_font_version     0x0013000B
#define status_$display_invalid_use_of_driver_procedure 0x00130004

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * dtty_$get_disp_type - Get current display type
 *
 * Returns the display type from the DTTY data block.
 * Uses A5 register as base pointer to data block.
 *
 * Returns:
 *   Display type (1 = 15", 2 = 19")
 *
 * Original address: 0x00E1D588
 */
uint16_t dtty_$get_disp_type(void);

/*
 * dtty_$clear_window - Clear display window
 *
 * Clears a display window region using SMD_$CLEAR_WINDOW.
 *
 * Parameters:
 *   region - Window region descriptor
 *   status_ret - Status return
 *
 * Original address: 0x00E1D592
 */
void dtty_$clear_window(void *region, status_$t *status_ret);

/*
 * dtty_$report_error - Report an error during DTTY initialization
 *
 * Prints an error message including status code and function name.
 *
 * Parameters:
 *   status - Error status code
 *   func_name - Name of function that failed
 *   context - Additional context string (may start with '$')
 *
 * Original address: 0x00E1D5B2
 */
void dtty_$report_error(status_$t status, const char *func_name, const char *context);

/*
 * dtty_$load_font - Load font to hidden display memory
 *
 * Loads a font into hidden display memory for fast text rendering.
 *
 * Parameters:
 *   font_ptr - Pointer to font data
 *   status_ret - Status return
 *
 * Original address: 0x00E1D668
 */
void dtty_$load_font(void **font_ptr, status_$t *status_ret);

#endif /* DTTY_INTERNAL_H */
