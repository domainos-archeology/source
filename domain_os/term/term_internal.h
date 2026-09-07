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
#include "tone/tone.h"  /* TONE_$CHANNEL (TERM_$DATA + 0x1268) */

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
 * Note: PROC2_$UID is declared in proc2/proc2.h
 * On m68k: #define PROC2_$UID (*(uid_t*)0xE7BE8C)
 * On other platforms: extern uid_t proc2_uid
 */

/*
 * Cells inside the TERM_$DATA module block (0x00E2C9F0, the map segment
 * "D E2C9F0 OS_TERM_INIT size = 1398").  TERM_$INIT reaches every one of them
 * with an absolute `pea`/`lea`, and each is at a fixed offset from the block
 * base, so they are aliases into TERM_$DATA rather than objects of their own
 * (bead source-wk2f).  The Ghidra label names are kept because the fields they
 * stand for are not all recovered yet.
 *
 * Only two windows of TERM_$DATA carry image bytes -- +0x00..+0xC3 and
 * +0x138D/+0x1390..+0x1394 (59 non-zero bytes in all) -- and term/term_data.c
 * now initialises both, so the aliases below that fall inside those windows
 * carry their image values and the rest are zero, as in the image.
 */
#define TERM_$DATA_AT(off) ((char *)&TERM_$DATA + (off))
#define DAT_00e2c9f0   TERM_$DATA_AT(0x0000)  /* TERM_$DATA base; SIO_$INIT_DESC's `desc_base` argument */
#define DAT_00e2ca30   TERM_$DATA_AT(0x0040)  /* serial-line SIO_$INIT_LINE handler block */
#define DAT_00e2ca48   TERM_$DATA_AT(0x0058)  /* console SIO_$INIT_DESC descriptor block */
#define DAT_00e2ca60   TERM_$DATA_AT(0x0070)  /* console SIO_$INIT_LINE handler block */
#define DAT_00e2caa0   TERM_$DATA_AT(0x00B0)  /* OS_TERM_INIT's sixth argument */
#define DAT_00e2cb48   TERM_$DATA_AT(0x0158)  /* console line record (the first per-line block) */
#define DAT_00e2cf1a   TERM_$DATA_AT(0x052A)  /* console drain-handler cell */
#define DAT_00e2d024   TERM_$DATA_AT(0x0634)  /* serial line 1 record */
#define DAT_00e2d3f6   TERM_$DATA_AT(0x0A06)  /* serial line 1 drain-handler cell */
#define DAT_00e2d500   TERM_$DATA_AT(0x0B10)  /* serial line 2 record */
#define DAT_00e2d8d2   TERM_$DATA_AT(0x0EE2)  /* serial line 2 drain-handler cell */
#define DAT_00e2d9e0   TERM_$DATA_AT(0x0FF0)  /* console SIO descriptor */
#define DAT_00e2da38   TERM_$DATA_AT(0x1048)  /* per-process table TERM_$INIT stamps with 0xFFFFFFFF */
#define DAT_00e2da58   TERM_$DATA_AT(0x1068)  /* serial line 1 SIO descriptor */
#define DAT_00e2daa4   TERM_$DATA_AT(0x10B4)  /* SIO2681 channel A parameters */
#define DAT_00e2dad0   TERM_$DATA_AT(0x10E0)  /* serial line 2 SIO descriptor */
#define DAT_00e2db1c   TERM_$DATA_AT(0x112C)  /* SIO2681 channel B parameters */
#define DAT_00e2db48   TERM_$DATA_AT(0x1158)  /* console drain-handler vector */
#define DAT_00e2db58   TERM_$DATA_AT(0x1168)  /* OS_TERM_INIT's first argument */
#define DAT_00e2dbf6   TERM_$DATA_AT(0x1206)  /* console SIO_$INIT_DESC's fifth argument */
#define DAT_00e2dc40   TERM_$DATA_AT(0x1250)  /* SIO6509 chip record */
#define DAT_00e2dc48   TERM_$DATA_AT(0x1258)  /* SIO2681 chip record */
#define DAT_00e2dc74   TERM_$DATA_AT(0x1284)  /* SIO2681 channel B record */
#define DAT_00e2dcb4   TERM_$DATA_AT(0x12C4)  /* TERM_$DATA.dtte[0].handler_ptr (0x12A0 + 0x24) */
#define DAT_00e2dcc8   TERM_$DATA_AT(0x12D8)  /* TERM_$DATA.dtte[1] */
#define DAT_00e2dd00   TERM_$DATA_AT(0x1310)  /* TERM_$DATA.dtte[2] */

/*
 * PTR_KBD_$RCV_00e2ca78 - TERM_$DATA + 0x88, the start of the console handler
 * array; the image longword there is 0x00E1CCC0 = KBD_$RCV.  TERM_$INIT passes
 * its address to SIO_$INIT_DESC.
 */
#define PTR_KBD_$RCV_00e2ca78 (TERM_$DATA.ptr_kbd_rcv)

/*
 * Handler function pointer cells inside the TERM_$DATA region (for TERM_$INIT).
 * They are fields of the block, not objects of their own: 0x00E2CA08 is
 * TERM_$DATA + 0x18 and 0x00E2CAB0 is TERM_$DATA + 0xC0, and both hold
 * 0x00E1B92A = TTY_$I_RCV in the image.  The names carry the TTY_ prefix only
 * because Ghidra labels them by the routine they point to.
 */
#define PTR_TTY_$I_RCV_00e2ca08 (TERM_$DATA.ptr_tty_i_rcv)
#define PTR_TTY_$I_RCV_00e2cab0 (TERM_$DATA.ptr_tty_i_rcv_alt)

/*
 * TONE_$CHANNEL (0x00E2DC58 = TERM_$DATA + 0x1268) is the SIO2681 channel-A
 * record inside this block; TERM_$INIT hands its address to SIO2681_$INIT and
 * TONE_$ENABLE forms it with `lea (0x1268,A5),A0` (0x00E1ACFC, A5 =
 * 0x00E2C9F0).  tone/tone.h aliases it onto TERM_$DATA.
 */

/*
 * Cells in the second OS_TERM_INIT block, the map segment
 * "D E35154 OS_TERM_INIT size = 5C" (0x00E35154..0x00E351B0).  They are the
 * SIO vtables and the SIO2681/SIO6509 configuration TERM_$INIT hands to the
 * chip initialisers, and they are objects of their own, not aliases.
 */
extern m68k_ptr_t DAT_00e35154[10];  /* 0x00E35154, 0x28: console SIO vtable */
extern m68k_ptr_t DAT_00e3517c[9];   /* 0x00E3517C, 0x24: serial SIO vtable  */
extern uint16_t   DAT_00e351a0[7];   /* 0x00E351A0, 0x0E: SIO2681 config     */
extern uint8_t    DAT_00e351ae[2];   /* 0x00E351AE, 0x02: SIO6509 config     */

/*
 * Two literal words in TERM_$INIT's own code region, just before the BITPAD
 * segment at 0x00E33224.  TERM_$INIT passes their addresses to SIO6509_$INIT
 * and SIO2681_$INIT.
 */
extern int16_t DAT_00e3321e;  /* 0x00E3321E: the word 2 */
extern int16_t DAT_00e33220;  /* 0x00E33220: the word 1 */

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
