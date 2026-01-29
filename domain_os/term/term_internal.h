/*
 * term/term_internal.h - Internal Terminal Definitions
 *
 * Contains internal functions, data, and types used only within
 * the terminal subsystem. External consumers should use term/term.h.
 */

#ifndef TERM_INTERNAL_H
#define TERM_INTERNAL_H

#include "term/term.h"
#include "tty/tty.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "kbd/kbd.h"

/*
 * ============================================================================
 * Internal Data Declarations
 * ============================================================================
 */

/*
 * DTTY_$CTRL - Default TTY control line
 * Original address: 0xe2e00e
 */
extern short DTTY_$CTRL;

/*
 * TERM_$DATA - Main terminal data structure
 * Original address: 0xe2c9f0
 */
extern term_data_t TERM_$DATA;

/*
 * Note: PROC2_UID is declared in proc2/proc2.h
 * On m68k: #define PROC2_UID (*(uid_t*)0xE7BE8C)
 * On other platforms: extern uid_t proc2_uid
 */

/*
 * Internal data arrays (DAT_* at fixed addresses)
 * These represent various terminal configuration and state tables.
 */
extern char DAT_00e2d9e0[];
extern char DAT_00e2db48[];
extern char DTTE[];
extern char DAT_00e2cb48[];
extern char DAT_00e2db58[];
extern char DAT_00e2caa0[];
extern char DAT_00e2ca60[];
extern char DAT_00e2cf1a[];
extern char DAT_00e2dc40[];
extern char DAT_00e2dbf6[];
extern char DAT_00e2ca48[];
extern char DAT_00e2d024[];
extern char DAT_00e2dcc8[];
extern char DAT_00e2da58[];
extern char DAT_00e2ca30[];
extern char DAT_00e2c9f0[];
extern char DAT_00e2dc58[];
extern char DAT_00e2d3f6[];
extern char DAT_00e2d500[];
extern char DAT_00e2dd00[];
extern char DAT_00e2dad0[];
extern char DAT_00e2dc74[];
extern char DAT_00e2d8d2[];
extern char DAT_00e2da38[];
extern char DAT_00e2daa4[];
extern char DAT_00e2db1c[];
extern char DAT_00e2dc48[];
extern char DAT_00e2dcb4[];
extern char DAT_00e351ae[];
extern char DAT_00e33220[];
extern char DAT_00e3321e[];

/*
 * ============================================================================
 * External Module Functions
 * ============================================================================
 */

/*
 * SIO module - Serial I/O
 */
void SIO_$K_SET_PARAM(short *line_ptr, void *params, void *mask, status_$t *status);
void SIO_$K_TIMED_BREAK(short *line_ptr, uint16_t *duration, status_$t *status);
void SIO_$K_INQ_PARAM(short *line_ptr, void *params, void *mask, status_$t *status);

/*
 * TPAD module - Touchpad/tablet input
 */
void TPAD_$DATA(void);

/*
 * Math helper
 */
short M_$OIS_WLW(long value, short modulus);

/*
 * ============================================================================
 * Additional Internal Declarations
 * ============================================================================
 */

/*
 * Status translation tables for TERM_$STATUS_CONVERT
 */
extern status_$t TERM_$STATUS_TRANSLATION_TABLE_33[];  // at 0xe2c9dc
extern status_$t TERM_$STATUS_TRANSLATION_TABLE_35[];  // at 0xe2c988
extern status_$t TERM_$STATUS_TRANSLATION_TABLE_36[];  // at 0xe2c9b0

/*
 * SUMA (Screen Update Manager) handler
 * SUMA_$RCV is a function declared in suma/suma.h
 */
#include "suma/suma.h"

/*
 * TERM_$KBD_STRING_LEN - Length of keyboard string data
 * Original address: 0xe1ac9c
 *
 * Note: TERM_$KBD_STRING_DATA is a macro defined in term.h that aliases
 * TERM_$DATA.kbd_string_data (at offset 0x1390 from TERM_$DATA base).
 */
extern uint16_t TERM_$KBD_STRING_LEN;

/*
 * Font reload function
 */
extern void DTTY_$RELOAD_FONT(void);

/*
 * Handler function pointer tables (for TERM_$INIT)
 */
extern void *PTR_TTY_$I_RCV_00e2cab0;
extern void *PTR_KBD_$RCV_00e2ca78;
extern void *PTR_TTY_$I_RCV_00e2ca08;

/*
 * External initialization functions (for TERM_$INIT)
 */
extern void OS_TERM_INIT(void *, void *, void **, void *, void **, void *);
extern void FUN_00e32b26(void *, void *, void **, void *);
extern void FUN_00e32bb8(void *, void *, void **, void **);
extern void FUN_00e32ab2(void *, void *, void *, void **, void **, void *, void **);
extern void FUN_00e32b76(void *, short);
extern void SIO6509_$INIT(void *, void *, void *, void **, void *);
extern void SIO2681_$INIT(void *, void *, void *, void **, void *, void *, void **, void *, void *);
/* TTY_$I_ENABLE_CRASH_FUNC is declared in tty/tty.h */
extern void SUMA_$INIT(void);

/* UID_$NIL is declared in base/base.h */

#endif /* TERM_INTERNAL_H */
