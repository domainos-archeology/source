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
 *   TERM_$KBD_STRING_LEN:               0xe1ac9c
 *   PTR_TERM_$ENQUEUE_TPAD_00e1ce90:    0xe1ce90
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
void *PTR_TERM_$ENQUEUE_TPAD_00e1ce90 = (void *)TERM_$ENQUEUE_TPAD;
