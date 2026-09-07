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
#include "dtty/dtty.h"          /* DTTY_$CTRL, DTTY_$RELOAD_FONT, DTTY_$INIT */
#include "sio/sio.h"            /* SIO_$K_*, SIO_$INIT_* */
#include "sio2681/sio2681.h"    /* SIO2681_$INIT */
#include "sio6509/sio6509.h"    /* SIO6509_$INIT */
#include "tpad/tpad.h"          /* TPAD_$DATA */
#include "suma/suma.h"          /* SUMA_$INIT, SUMA_$RCV */
#include "math/math.h"          /* M$OIS$WLW */
#include "os/os.h"              /* OS_TERM_INIT */
#include "uid/uid.h"   /* UID_$NIL */
#include "uid/uid.h"   /* UID_$NIL */

/*
 * ============================================================================
 * Internal Data Declarations
 * ============================================================================
 */

/* DTTY_$CTRL (0xe2e00e) is declared in dtty/dtty.h */

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
/* DTTE is TERM_$DATA.dtte; see the alias in term/term.h. */
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
extern char TONE_$CHANNEL[];
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
extern char DAT_00e35154[];  /* SIO vtable for console/keyboard line */
extern char DAT_00e3517c[];  /* SIO vtable for serial lines */
extern char DAT_00e33220[];
extern char DAT_00e3321e[];

/*
 * External module functions used by term/ come from the owning subsystems'
 * public headers (included above):
 *   SIO_$K_SET_PARAM / SIO_$K_TIMED_BREAK / SIO_$K_INQ_PARAM - sio/sio.h
 *   TPAD_$DATA                                               - tpad/tpad.h
 *   M$OIS$WLW (16-bit modulus helper)                        - math/math.h
 */

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
 * TERM_$KBD_STRING_LEN - Length of keyboard string data
 * Original address: 0xe1ac9c
 *
 * Note: TERM_$KBD_STRING_DATA is a macro defined in term.h that aliases
 * TERM_$DATA.kbd_string_data (at offset 0x1390 from TERM_$DATA base).
 */
extern uint16_t TERM_$KBD_STRING_LEN;

/* DTTY_$RELOAD_FONT is declared in dtty/dtty.h */

/*
 * Handler function pointer cells inside the TERM_$DATA region (for TERM_$INIT).
 * These are term-owned data (defined in term/term_data.c); the names carry the
 * TTY_/KBD_ prefix only because Ghidra labels them by the routine they point
 * to.  PTR_KBD_$RCV_00e2ca78 is the start of the console handler array at
 * TERM_$DATA + 0x88.
 */
extern void *PTR_TTY_$I_RCV_00e2cab0;
extern void *PTR_KBD_$RCV_00e2ca78;
extern void *PTR_TTY_$I_RCV_00e2ca08;

/*
 * SIO2681 configuration block passed as the 10th argument of SIO2681_$INIT
 * by TERM_$INIT (0xe35154 + 0x4c; see sio2681/sio2681.h).
 */
extern char DAT_00e351a0[];

/*
 * External initialization functions used by TERM_$INIT come from:
 *   OS_TERM_INIT                                   - os/os.h
 *   SIO_$INIT_LINE / _DRAIN_HANDLER / _DESC / _DTTE - sio/sio.h
 *   SIO6509_$INIT                                  - sio6509/sio6509.h
 *   SIO2681_$INIT                                  - sio2681/sio2681.h
 *   TTY_$I_ENABLE_CRASH_FUNC                       - tty/tty.h
 *   SUMA_$INIT                                     - suma/suma.h
 */

/* UID_$NIL is declared in base/base.h */

#endif /* TERM_INTERNAL_H */
