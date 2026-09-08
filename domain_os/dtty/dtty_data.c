/*
 * dtty/dtty_data.c - Display TTY Module Global Data
 *
 * Defines global variables used by the DTTY subsystem.
 *
 * Original M68K memory layout at 0x00E2E00C:
 *   Offset  Size  Name
 *   0x00    2     DTTY_$DISP_TYPE   - Display type (1=15", 2=19")
 *   0x02    2     DTTY_$CTRL        - Control word from init
 *   0x04    1     (internal)        - Unknown flag
 *   0x05    1     (padding)
 *   0x06    1     (internal)        - Internal flag
 *   0x07    1     (padding)
 *   0x08    1     DTTY_$USE_DTTY    - Use display TTY flag
 */

#include "dtty/dtty_internal.h"

/*
 * Display type code (1 = 15" portrait, 2 = 19" landscape)
 * Original address: 0x00E2E00C
 */
uint16_t DTTY_$DISP_TYPE = 0;

/*
 * Control word passed during initialization
 * Original address: 0x00E2E00E
 */
uint16_t DTTY_$CTRL = 0;

/*
 * Module-local flag bytes at 0x00E2E010 and 0x00E2E012.
 *
 * DTTY_$INIT is the only code in this image that touches either
 * (`clr.b (0x4,A2)` at 0x00E34BF6 and `st (0x6,A2)` at 0x00E34BF2 with
 * A2 = 0xE2E00C); the SAU2 map gives the DTTY data segment at E2E00C a size
 * of 0xC and names no symbol at these two offsets, so the names here are
 * module-local (bead source-4km0).
 */
int8_t DTTY_FLAG_04 = 0;
int8_t DTTY_FLAG_06 = 0;

/*
 * Display TTY active flag
 * 0xFF = use display TTY, 0x00 = disabled
 * Original address: 0x00E2E014
 */
int8_t DTTY_$USE_DTTY = 0;

/*
 * Standard font pointer
 * Points to the font data loaded for DTTY text output
 * Original address: 0x00E82744
 */
void *DTTY_$STD_FONT_P = NULL;
