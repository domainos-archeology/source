/*
 * vfmt/vfmt_internal.h - Internal VFMT definitions
 *
 * Contains internal types and helper function declarations used
 * by the VFMT formatting implementation.
 */

#ifndef VFMT_INTERNAL_H
#define VFMT_INTERNAL_H

#include "vfmt/vfmt.h"
#include "math/math.h"

/*
 * Format specifier character codes
 */
#define VFMT_CHAR_PERCENT   0x25    /* '%' - format specifier start */
#define VFMT_CHAR_A         0x41    /* 'A' - ASCII string */
#define VFMT_CHAR_D         0x44    /* 'D' - Decimal */
#define VFMT_CHAR_H         0x48    /* 'H' - Hexadecimal */
#define VFMT_CHAR_O         0x4F    /* 'O' - Octal */
#define VFMT_CHAR_T         0x54    /* 'T' - Tab to position */
#define VFMT_CHAR_X         0x58    /* 'X' - Repeat character */
#define VFMT_CHAR_DOLLAR    0x24    /* '$' - End marker */
#define VFMT_CHAR_DOT       0x2E    /* '.' - Alternate end */
#define VFMT_CHAR_SLASH     0x2F    /* '/' - Flush output */
#define VFMT_CHAR_LPAREN    0x28    /* '(' - Start repeat group */
#define VFMT_CHAR_RPAREN    0x29    /* ')' - End repeat group */

/*
 * Modifier character codes
 */
#define VFMT_MOD_L          0x4C    /* 'L' - Lowercase */
#define VFMT_MOD_U          0x55    /* 'U' - Uppercase */
#define VFMT_MOD_Z          0x5A    /* 'Z' - Zero-strip */
#define VFMT_MOD_W          0x57    /* 'W' - Word (16-bit) */
#define VFMT_MOD_M          0x4D    /* 'M' - Width follows */

/*
 * The nested procedures of VFMT_$MAIN (vfmt_output_char 0x00E6A9F6,
 * vfmt_get_next_arg 0x00E6AA38, vfmt_parse_number 0x00E6AA4C,
 * vfmt_parse_number_after_m 0x00E6AAB6 and vfmt_format_number 0x00E6A704)
 * reach VFMT_$MAIN's frame through the Pascal static link and are static
 * functions of vfmt/main.c; nothing outside that file calls them.
 */

#endif /* VFMT_INTERNAL_H */
