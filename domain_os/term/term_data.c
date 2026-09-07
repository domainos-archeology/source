/*
 * term/term_data.c - Terminal Subsystem Global Data Definitions
 *
 * Defines the global data structures for the TERM subsystem.
 * Original addresses:
 *   TERM_$DATA:                          0xe2c9f0
 *   TERM_$TPAD_BUFFER:                   0xe2de3c
 *   TERM_$STATUS_TRANSLATION_TABLE_33:   0xe2c9dc
 *   TERM_$STATUS_TRANSLATION_TABLE_35:   0xe2c988
 *   TERM_$STATUS_TRANSLATION_TABLE_36:   0xe2c9b0
 *   TERM_$KBD_STRING_LEN:                0xe1ac9c
 *   PTR_TERM_$ENQUEUE_TPAD_00e1ce90:     0xe1ce90
 *   PTR_TTY_$I_RCV_00e2cab0:             0xe2cab0
 *   PTR_TTY_$I_RCV_00e2ca08:             0xe2ca08
 */

#include "term/term_internal.h"
#include "suma/suma.h"

/*
 * TERM_$DATA - Main terminal data structure
 *
 * The kbd_string_data field at offset 0x1390 is statically initialized
 * with the keyboard string sequence { 0xff, 0x00, 0xff, 0x12, 0x21 }.
 */
term_data_t TERM_$DATA = {
    .kbd_string_data = { 0xff, 0x00, 0xff, 0x12, 0x21 }
};

/*
 * TERM_$TPAD_BUFFER - Tablet pad circular sample buffer
 *
 * Zero-initialized; used by KBD and SUMA subsystems.
 */
tpad_buffer_t TERM_$TPAD_BUFFER = {0};

/*
 * Status translation tables for TERM_$STATUS_CONVERT
 *
 * These tables map subsystem-specific error codes to standard status_$t values.
 */

/* Table 33: 5 entries at 0xe2c9dc */
status_$t TERM_$STATUS_TRANSLATION_TABLE_33[5] = {
    0x000b0010, 0x000b0004, 0x000b000d, 0x000b0007, 0x000b0008
};

/* Table 35: 10 entries at 0xe2c988 */
status_$t TERM_$STATUS_TRANSLATION_TABLE_35[10] = {
    0x00000000, 0x000b0004, 0x000b000d, 0x000b0007,
    0x000b0001, 0x000b0002, 0x000b0003, 0x000b0006,
    0x00000000, 0x000b0005
};

/* Table 36: 11 entries at 0xe2c9b0 */
status_$t TERM_$STATUS_TRANSLATION_TABLE_36[11] = {
    0x00000000, 0x000b0004, 0x000b000d, 0x000b0007,
    0x000b0009, 0x000b000a, 0x000b000b, 0x000b000c,
    0x000b000f, 0x000b0005, 0x000b0006
};

/*
 * TERM_$KBD_STRING_LEN - Length of keyboard string data
 *
 * Value = 5, matching the 5-byte sequence in TERM_$DATA.kbd_string_data.
 * Original address: 0xe1ac9c (embedded constant after SET_REAL_LINE_DISCIPLINE)
 */
uint16_t TERM_$KBD_STRING_LEN = 5;

/*
 * PTR_TERM_$ENQUEUE_TPAD_00e1ce90 - Function pointer to TERM_$ENQUEUE_TPAD
 *
 * Original address: 0xe1ce90
 */
DXM_$DEFINE_CALLBACK_CELL(PTR_TERM_$ENQUEUE_TPAD_00e1ce90, TERM_$ENQUEUE_TPAD);

/*
 * PTR_TTY_$I_RCV_00e2cab0 - Function pointer to TTY_$I_RCV
 *
 * Used by TERM_$INIT for console handler setup.
 *
 * Original address: 0xe2cab0
 */
void *PTR_TTY_$I_RCV_00e2cab0 = (void *)TTY_$I_RCV;

/*
 * PTR_TTY_$I_RCV_00e2ca08 - Function pointer to TTY_$I_RCV
 *
 * Used by TERM_$INIT for serial port handler setup.
 *
 * Original address: 0xe2ca08
 */
void *PTR_TTY_$I_RCV_00e2ca08 = (void *)TTY_$I_RCV;

/*
 * ============================================================================
 * Term_Manual_Stop_err
 * ============================================================================
 *
 * 0x00E1CE8C, a status cell in the literal pool at the tail of KBD_$RCV
 * (map segment "I E1C9FC KBD size = 4F0"; KBD_$CRASH_INIT follows at
 * 0x00E1CE94).  Image bytes: 00 0B 00 08.
 */
status_$t Term_Manual_Stop_err = 0x000B0008;

/*
 * ============================================================================
 * The second OS_TERM_INIT block, 0x00E35154
 * ============================================================================
 *
 * Map: "D E35154 OS_TERM_INIT size = 5C", running 0x00E35154..0x00E351B0
 * (the TTY segment starts there).  It exports no interior symbol; the four
 * objects below fill it exactly, and their extents come from the addresses
 * TERM_$INIT pushes: 0x00E35154, 0x00E3517C, 0x00E351A0 and 0x00E351AE.
 * Read with `gsk read 0x00E35154 0x5C`.
 */

/*
 * DAT_00e35154 - the console line's SIO vtable, SIO_$INIT_DESC's last
 * argument.  0x28 bytes = 10 longwords; SIO_$INIT_DESC copies the entries
 * from offset 0x14 on.
 *   +0x14 0x00E1D586  +0x18 0x00E72656  +0x1C 0x00E72668
 */
m68k_ptr_t DAT_00e35154[10] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00E1D586, 0x00E72656, 0x00E72668, 0x00000000, 0x00000000,
};
#if defined(ARCH_M68K)
_Static_assert(sizeof(DAT_00e35154) == 0x28,
               "DAT_00e35154: 0x00E35154..0x00E3517C");
#endif

/*
 * DAT_00e3517c - the serial lines' SIO vtable, used for both line 1 and
 * line 2.  0x24 bytes = 9 longwords.
 *   +0x14 0x00E1D4FC  +0x18 0x00E1D250  +0x1C 0x00E725B0  +0x20 0x00E1D114
 */
m68k_ptr_t DAT_00e3517c[9] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00E1D4FC, 0x00E1D250, 0x00E725B0, 0x00E1D114,
};
#if defined(ARCH_M68K)
_Static_assert(sizeof(DAT_00e3517c) == 0x24,
               "DAT_00e3517c: 0x00E3517C..0x00E351A0");
#endif

/*
 * DAT_00e351a0 - SIO2681_$INIT's `config` argument, 0x0E bytes = 7 words.
 */
uint16_t DAT_00e351a0[7] = {
    0x8f00, 0xf700, 0x8f00, 0x0000, 0x8f00, 0xf700, 0x8f00,
};
#if defined(ARCH_M68K)
_Static_assert(sizeof(DAT_00e351a0) == 0x0E,
               "DAT_00e351a0: 0x00E351A0..0x00E351AE");
#endif

/*
 * DAT_00e351ae - SIO6509_$INIT's `config` argument, the last 2 bytes of the
 * block (the TTY segment starts at 0x00E351B0).
 */
uint8_t DAT_00e351ae[2] = { 0x03, 0xd9 };

/*
 * ============================================================================
 * Literal words in TERM_$INIT's code region
 * ============================================================================
 *
 * They sit between TERM_$INIT's `rts` at 0x00E3321C and the BITPAD segment at
 * 0x00E33224 (0x00E33222 holds the two filler bytes 20 48).  TERM_$INIT hands
 * their addresses to SIO6509_$INIT and SIO2681_$INIT.
 */

/* 0x00E3321E: the word 2 - SIO6509_$INIT's chip_num_ptr. */
int16_t DAT_00e3321e = 2;

/* 0x00E33220: the word 1 - SIO6509_$INIT's int_vec_ptr and both of
 * SIO2681_$INIT's first two arguments. */
int16_t DAT_00e33220 = 1;

