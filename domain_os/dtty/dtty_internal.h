/*
 * dtty/dtty_internal.h - Display TTY Module Internal API
 *
 * Internal functions and data used within the DTTY subsystem.
 */

#ifndef DTTY_INTERNAL_H
#define DTTY_INTERNAL_H

#include "dtty/dtty.h"
#include "smd/smd.h"          /* SMD_$COPY_FONT_TO_MD_HDM, status_$display_* */
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
 * DTTY module data block, 0x00E2E00C .. 0x00E2E017 (the SAU2 map's "D
 * E2E00C DTTY size = C").  DTTY_$INIT reaches all of it through
 * `movea.l #0xe2e00c,A2` at 0x00E34BE4:
 *
 *   +0x00  0x00E2E00C  DTTY_$DISP_TYPE  word, `move.w D0w,(A2)`
 *   +0x02  0x00E2E00E  DTTY_$CTRL       word, map symbol
 *   +0x04  0x00E2E010  DTTY_FLAG_04     byte, `clr.b (0x4,A2)` 0x00E34BF6
 *   +0x06  0x00E2E012  DTTY_FLAG_06     byte, `st (0x6,A2)`    0x00E34BF2
 *   +0x08  0x00E2E014  DTTY_$USE_DTTY   byte, map symbol
 *
 * The two flag bytes have no symbol in the map, so they are module-local
 * names here.  They are written by DTTY_$INIT and by nothing else in this
 * image (`gsk xrefs to 00e2e010` / `00e2e012` each list exactly that one
 * WRITE), which is why they were missing from the C until bead source-4km0.
 */
extern int8_t DTTY_FLAG_04;   /* 0x00E2E010 */
extern int8_t DTTY_FLAG_06;   /* 0x00E2E012 */

/*
 * Hardware-specific display status registers
 *
 * These memory locations contain display status bits.
 * Bit 0 indicates display hardware presence.
 *
 * DTTY_$INIT reads them with `move.w (0x00fc0066).l,D0w` (0x00E34C20) and
 * `move.w (0x00fdebe6).l,D0w` (0x00E34C3A).  The accessors go through
 * ARCH_VA_TO_PTR - the identity cast on m68k - so a host test can point
 * ARCH_HOST_VA_BASE at an arena and supply the register value.
 */
#define DISP_15_STATUS_ADDR     0x00FC0066  /* 15" display status */
#define DISP_19_STATUS_ADDR     0x00FDEBE6  /* 19" display status */

#define DTTY_DISP_STATUS_15() \
    (*(volatile uint16_t *)ARCH_VA_TO_PTR(DISP_15_STATUS_ADDR))
#define DTTY_DISP_STATUS_19() \
    (*(volatile uint16_t *)ARCH_VA_TO_PTR(DISP_19_STATUS_ADDR))

/*
 * Display unit number for SMD association
 */
#define DTTY_DISPLAY_UNIT       1

/* The SMD status codes DTTY raises (0x0013000B, 0x00130004) are declared in
 * smd/smd.h (included above -- bead source-3uo). */

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
