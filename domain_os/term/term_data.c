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
 */

#include "term/term_internal.h"
#include "suma/suma.h"

/*
 * TERM_$DATA - the OS_TERM_INIT module block, 0x00E2C9F0, 0x1398 bytes
 * (map: "D E2C9F0 OS_TERM_INIT size = 1398", 0x00E2C9F0..0x00E2DD88, with
 * TTY_$SPIN_LOCK at +0x1384 and TERM_$MAX_DTTE at +0x1388 as its two
 * interior symbols).
 *
 * Only 59 of those bytes are non-zero in the image, in two windows -- the
 * handler and descriptor blocks at +0x00..+0xC3, and +0x138D plus the
 * keyboard string at +0x1390..+0x1394.  Everything else (the per-line
 * records at +0x158, the SIO descriptors, the DTTE array) is zero-filled and
 * is built at run time by TERM_$INIT.  Every value below is the image
 * longword/word at that offset; the pointer cells are given as addresses,
 * with the SAU2 map symbol they name in the comment.
 */
term_data_t TERM_$DATA = {
    .w_02 = 0x0009,                      /* 0x00E2C9F2 */
    .w_06 = 0x000c,                      /* 0x00E2C9F6 */
    .w_0c = 0x000e,                      /* 0x00E2C9FC */
    .w_0e = 0x000e,                      /* 0x00E2C9FE */
    .w_10 = 0x0003,                      /* 0x00E2CA00 */
    .w_12 = 0x0001,                      /* 0x00E2CA02 */
    .ptr_tty_i_rcv   = 0x00E1B92A,       /* 0x00E2CA08 TTY_$I_RCV */
    .ptr_tty_i_drain = 0x00E1B394,       /* 0x00E2CA0C TTY_$I_OUTPUT_BUFFER_DRAINED */
    .ptr_tty_i_hup   = 0x00E1BECE,       /* 0x00E2CA10 TTY_$I_HUP */
    .ptr_tty_i_int   = 0x00E1BEA8,       /* 0x00E2CA14 TTY_$I_INTERRUPT */
    .ptr_tty_i_err   = 0x00E1BE08,       /* 0x00E2CA18 TTY_$I_ERR */
    .ptr_sio_i_tstart       = 0x00E1C7A8,/* 0x00E2CA38 SIO_$I_TSTART */
    .ptr_sio_i_inhibit_xmit = 0x00E1C9CE,/* 0x00E2CA3C SIO_$I_INHIBIT_XMIT */
    .ptr_sio_i_inhibit_rcv  = 0x00E1C94A,/* 0x00E2CA40 SIO_$I_INHIBIT_RCV */
    .ptr_sio_i_err          = 0x00E67D9C,/* 0x00E2CA44 SIO_$I_ERR */
    .w_5a = 0x0009,                      /* 0x00E2CA4A */
    .w_64 = 0x0007,                      /* 0x00E2CA54 */
    .w_66 = 0x0007,                      /* 0x00E2CA56 */
    .w_68 = 0x0003,                      /* 0x00E2CA58 */
    .w_6a = 0x0001,                      /* 0x00E2CA5A */
    .w_6c = 0x0003,                      /* 0x00E2CA5C */
    .ptr_dtty_tstart = 0x00E1D6D0,       /* 0x00E2CA68 DTTY_$TSTART */
    .ptr_kbd_rcv     = 0x00E1CCC0,       /* 0x00E2CA78 KBD_$RCV */
    .ptr_kbd_drain   = 0x00E1CE96,       /* 0x00E2CA7C KBD_$OUTPUT_BUFFER_DRAINED */
    .ptr_sio_i_tstart_b4 = 0x00E1C7A8,   /* 0x00E2CAA4 SIO_$I_TSTART */
    .ptr_tty_i_rcv_alt   = 0x00E1B92A,   /* 0x00E2CAB0 TTY_$I_RCV */
    .b_138d = 0xff,                      /* 0x00E2DD7D; no reader in the image */
    .kbd_string_data = { 0xff, 0x00, 0xff, 0x12, 0x21 }, /* 0x00E2DD80 */
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

