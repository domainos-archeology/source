/*
 * dtty/init.c - DTTY_$INIT implementation
 *
 * Initializes the Display TTY subsystem.
 *
 * Original address: 0x00E34BD0
 */

#include "dtty/dtty_internal.h"

/*
 * Window region structure used for SMD_$CLEAR_WINDOW
 * This defines a rectangular region on the display.
 */
typedef struct {
    uint16_t x1;        /* Left X coordinate */
    uint16_t y1;        /* Top Y coordinate */
    uint16_t x2;        /* Right X coordinate */
    uint16_t y2;        /* Bottom Y coordinate */
} dtty_window_t;

/*
 * Display dimensions for 15" and 19" displays
 * 15" display: 800x1024 (portrait orientation)
 * 19" display: 1024x800 (landscape orientation)
 */
#define DISP_15_WIDTH   800
#define DISP_15_HEIGHT  1024
#define DISP_19_WIDTH   1024
#define DISP_19_HEIGHT  800

/*
 * DTTY_$INIT - Initialize Display TTY subsystem
 *
 * Initializes the display terminal subsystem:
 * 1. Queries display type from SMD
 * 2. Determines if DTTY should be enabled based on mode and hardware
 * 3. If enabled, associates with SMD, clears the display window,
 *    and loads the standard font
 *
 * Parameters:
 *   mode - Pointer to mode word:
 *          0: Auto-detect (use DTTY if display hardware present)
 *          1: Force DTTY enabled
 *          other: DTTY disabled
 *   ctrl - Pointer to control word (stored in DTTY_$CTRL)
 *
 * The function determines whether to enable DTTY based on:
 * - Mode value from parameter
 * - Hardware status bit (bit 0 of display status register)
 *
 * Logic for DTTY_$USE_DTTY:
 *   if (mode == 1)      -> USE_DTTY = 0xFF (force enabled)
 *   elif (mode == 0 && hw_present) -> USE_DTTY = 0xFF
 *   else                -> USE_DTTY = 0x00 (disabled)
 *
 * Assembly (0x00E34BD0):
 *   link.w  A6,#-0x44
 *   movem.l {A2 D3 D2},-(SP)
 *   movea.l (0x8,A6),A0          ; A0 = mode
 *   movea.l (0xc,A6),A1          ; A1 = ctrl
 *   move.w  (A0),D2w             ; D2 = *mode
 *   move.w  (A1),D0w             ; D0 = *ctrl
 *   movea.l #0xe2e00c,A2         ; A2 = DTTY data block base
 *   move.w  D0w,(0x2,A2)         ; DTTY_$CTRL = *ctrl
 *   st      (0x8,A2)             ; DTTY_$USE_DTTY = 0xFF
 *   st      (0x6,A2)             ; internal flag = 0xFF
 *   clr.b   (0x4,A2)             ; internal flag2 = 0
 *   pea     (0xf2,PC)            ; Push display unit 1
 *   jsr     SMD_$INQ_DISP_TYPE
 *   ...
 */
void DTTY_$INIT(int16_t *mode, uint16_t *ctrl)
{
    int16_t mode_val;
    uint16_t disp_status;
    uint16_t display_unit;
    dtty_window_t window;
    status_$t status;
    const char *error_func;

    /* Save mode value and control word */
    mode_val = *mode;
    DTTY_$CTRL = *ctrl;

    /* Initialize flags: assume DTTY is enabled */
    DTTY_$USE_DTTY = (int8_t)true;

    /* Query display type from SMD subsystem */
    display_unit = DTTY_DISPLAY_UNIT;
    DTTY_$DISP_TYPE = SMD_$INQ_DISP_TYPE(&display_unit);

    /* Set up window dimensions based on display type */
    if (DTTY_$DISP_TYPE == DTTY_DISP_TYPE_15_INCH) {
        /* 15" display: 800x1024 portrait */
        window.x1 = 0;
        window.y1 = 0;
        window.x2 = DISP_15_WIDTH - 1;   /* 799 */
        window.y2 = DISP_15_HEIGHT - 1;  /* 1023 = 0x3FF */

        /* Read hardware status from 15" display register */
        disp_status = *(volatile uint16_t *)DISP_15_STATUS_ADDR;
    }
    else if (DTTY_$DISP_TYPE == DTTY_DISP_TYPE_19_INCH) {
        /* 19" display: 1024x800 landscape */
        window.x1 = 0;
        window.y1 = 0;
        window.x2 = DISP_19_WIDTH - 1;   /* 1023 = 0x3FF */
        window.y2 = DISP_19_HEIGHT - 1;  /* 799 */

        /* Read hardware status from 19" display register */
        disp_status = *(volatile uint16_t *)DISP_19_STATUS_ADDR;
    }
    else {
        /* Unknown display type - cannot initialize */
        return;
    }

    /*
     * Determine if DTTY should be enabled:
     *
     * Mode 1: Force DTTY on
     * Mode 0: Use DTTY if hardware is present (bit 0 of status set)
     * Other modes: DTTY disabled
     *
     * Original assembly logic:
     *   btst.l  #0,D0          ; Test bit 0 of disp_status
     *   sne     D1b            ; D1 = (bit0 != 0) ? 0xFF : 0x00
     *   tst.w   D2w            ; Test mode
     *   seq     D3b            ; D3 = (mode == 0) ? 0xFF : 0x00
     *   and.b   D3b,D1b        ; D1 = D1 & D3 (mode==0 && hw_present)
     *   cmpi.w  #1,D2w         ; Compare mode with 1
     *   seq     D3b            ; D3 = (mode == 1) ? 0xFF : 0x00
     *   or.b    D3b,D1b        ; D1 = D1 | D3
     *   move.b  D1b,(0x8,A2)   ; DTTY_$USE_DTTY = D1
     */
    if (mode_val == 1) {
        /* Force DTTY enabled */
        DTTY_$USE_DTTY = (int8_t)true;
    }
    else if (mode_val == 0 && (disp_status & 0x01)) {
        /* Auto mode and hardware present */
        DTTY_$USE_DTTY = (int8_t)true;
    }
    else {
        /* Disabled */
        DTTY_$USE_DTTY = (int8_t)false;
    }

    /* If DTTY is disabled, we're done */
    /* Original: bmi.b done (tests sign bit of USE_DTTY byte) */
    if (DTTY_$USE_DTTY >= 0) {
        return;
    }

    /* Clear control word when initializing display mode */
    DTTY_$CTRL = 0;

    /* Associate display with current process */
    display_unit = DTTY_DISPLAY_UNIT;
    SMD_$ASSOC(&display_unit, &PROC1_$CURRENT, &status);
    if (status != status_$ok) {
        error_func = "smd_$assoc";
        goto report_error;
    }

    /* Clear the display window */
    dtty_$clear_window(&window, &status);
    if (status != status_$ok) {
        error_func = "dtty_$clear_window";
        goto report_error;
    }

    /* Load the standard font into hidden display memory */
    dtty_$load_font(&DTTY_$STD_FONT_P, &status);
    if (status != status_$ok) {
        error_func = "smd_$copy_font_to_md_hdm";
        goto report_error;
    }

    /* Initialization successful */
    return;

report_error:
    dtty_$report_error(status, error_func, "$");
}
