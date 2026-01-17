/*
 * dtty/dtty.h - Display TTY Module Public API
 *
 * Provides display terminal handling for Domain/OS. The DTTY subsystem
 * provides a low-level console interface that can output directly to
 * the display hardware or through the PROM monitor.
 *
 * The subsystem operates in two modes:
 *   1. PROM mode: Uses PROM entry points for character output (DTTY_$PUTC, DTTY_$CLEAR_SCREEN)
 *   2. Display mode: Uses SMD (Screen Management Display) for graphical output
 *
 * Original addresses: 0x00E1D588 - 0x00E34CEE
 */

#ifndef DTTY_H
#define DTTY_H

#include "base/base.h"

/*
 * ============================================================================
 * Display Type Constants
 * ============================================================================
 */
#define DTTY_DISP_TYPE_15_INCH      1   /* 15" display (portrait: 800x1024) */
#define DTTY_DISP_TYPE_19_INCH      2   /* 19" display (landscape: 1024x800) */

/*
 * ============================================================================
 * PROM Entry Points
 * ============================================================================
 * These low memory addresses contain pointers to PROM routines.
 * The PROM provides basic console I/O independent of the OS.
 */
#define PROM_PUTC_ADDR      0x00000108  /* Character output routine */
#define PROM_CLEAR_ADDR     0x00000140  /* Screen clear routine */

/*
 * ============================================================================
 * Global Variables
 * ============================================================================
 */

/*
 * DTTY_$USE_DTTY - Display TTY active flag
 *
 * 0xFF (-1) when the display TTY should be used for console output.
 * 0x00 when display TTY is disabled.
 *
 * Original address: 0x00E2E014
 */
extern int8_t DTTY_$USE_DTTY;

/*
 * DTTY_$CTRL - Display TTY control word
 *
 * Control flags passed during initialization.
 *
 * Original address: 0x00E2E00E
 */
extern uint16_t DTTY_$CTRL;

/*
 * DTTY_$DISP_TYPE - Current display type
 *
 * Stores the display type (1=15", 2=19").
 *
 * Original address: 0x00E2E00C
 */
extern uint16_t DTTY_$DISP_TYPE;

/*
 * DTTY_$STD_FONT_P - Standard font pointer
 *
 * Pointer to the standard display font used for DTTY output.
 *
 * Original address: 0x00E82744
 */
extern void *DTTY_$STD_FONT_P;

/*
 * ============================================================================
 * TSTART Callback Structure
 * ============================================================================
 * Used by DTTY_$TSTART to manage string buffer output with callbacks.
 */
typedef struct dtty_tstart_t {
    void     (*callback)(void *);   /* 0x00: Callback function */
    void     *callback_arg;         /* 0x04: Callback argument */
    void     *buffer_info;          /* 0x08: Pointer to buffer descriptor */
} dtty_tstart_t;

/*
 * Buffer descriptor used by TSTART
 * Describes a circular or linear string buffer.
 */
typedef struct dtty_buffer_t {
    uint16_t    current;            /* 0x00: Current position in buffer */
    uint16_t    target;             /* 0x02: Target position to output to */
    uint16_t    end;                /* 0x04: End of valid data in buffer */
    char        data[1];            /* 0x06: Start of character data (variable length) */
} dtty_buffer_t;

/*
 * ============================================================================
 * Initialization Functions
 * ============================================================================
 */

/*
 * DTTY_$INIT - Initialize Display TTY subsystem
 *
 * Initializes the display terminal subsystem. Queries display type,
 * associates with the SMD display driver, clears the window, and
 * loads the standard font.
 *
 * Parameters:
 *   mode - Pointer to mode word:
 *          0: Auto-detect (use DTTY if display available)
 *          1: Force DTTY enabled
 *          other: DTTY disabled
 *   ctrl - Pointer to control word (stored in DTTY_$CTRL)
 *
 * Original address: 0x00E34BD0
 */
void DTTY_$INIT(int16_t *mode, uint16_t *ctrl);

/*
 * DTTY_$RELOAD_FONT - Reload the standard font
 *
 * Reloads the standard font into hidden display memory.
 * Called when the font may have been corrupted or needs refresh.
 *
 * Original address: 0x00E1D73A
 */
void DTTY_$RELOAD_FONT(void);

/*
 * ============================================================================
 * Output Functions
 * ============================================================================
 */

/*
 * DTTY_$PUTC - Output a single character
 *
 * Outputs a character through the PROM character output routine.
 * Saves and restores all caller-saved registers.
 *
 * Parameters:
 *   ch - Pointer to character to output
 *
 * Original address: 0x00E2E018
 */
void DTTY_$PUTC(char *ch);

/*
 * DTTY_$CLEAR_SCREEN - Clear the display
 *
 * Clears the entire display through the PROM clear routine.
 * Uses function code 3 for full screen clear.
 *
 * Original address: 0x00E2E048
 */
void DTTY_$CLEAR_SCREEN(void);

/*
 * DTTY_$WRITE_STRING - Write a string to the display
 *
 * Outputs a counted string to the display by calling DTTY_$PUTC
 * for each character.
 *
 * Parameters:
 *   str - Pointer to string data (1-indexed, Pascal-style)
 *   len - Pointer to string length
 *
 * Original address: 0x00E1D690
 */
void DTTY_$WRITE_STRING(char *str, int16_t *len);

/*
 * DTTY_$TSTART - Terminal start with callback
 *
 * Outputs buffered string data and then calls a callback function.
 * Used for deferred output handling in the terminal subsystem.
 *
 * The function examines the buffer descriptor and outputs any
 * pending characters, handling wrap-around for circular buffers.
 *
 * Parameters:
 *   tstart - Pointer to tstart structure with callback and buffer info
 *
 * Original address: 0x00E1D6D0
 */
void DTTY_$TSTART(dtty_tstart_t *tstart);

#endif /* DTTY_H */
