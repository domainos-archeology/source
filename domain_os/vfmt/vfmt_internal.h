/*
 * vfmt/vfmt_internal.h - Internal VFMT definitions
 *
 * Contains internal types and helper function declarations used
 * by the VFMT formatting implementation.
 */

#ifndef VFMT_INTERNAL_H
#define VFMT_INTERNAL_H

#include "vfmt/vfmt.h"

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
 * VFMT context structure
 *
 * Maintains state during format string processing.
 * Passed implicitly via the frame pointer in the original Pascal code.
 */
typedef struct {
    const char *format;          /* Format string (1-based indexing) */
    char *output;                /* Output buffer */
    int16_t max_len;             /* Maximum output length */
    int16_t *out_len_p;          /* Pointer to output length */
    void **args;                 /* Current argument pointer */
    int16_t format_pos;          /* Current position in format string */
    int16_t arg_count;           /* Number of arguments consumed */
    int16_t max_written;         /* Maximum position written to */
    int16_t repeat_count;        /* Current repeat count */
    int16_t repeat_pos;          /* Position to repeat from */
} vfmt_ctx_t;

/*
 * vfmt_output_char - Output a single character
 *
 * Writes a character to the output buffer if space remains.
 * This is a nested sub-procedure in the original Pascal.
 *
 * Original address: 0x00e6a9f6
 */
void vfmt_output_char(vfmt_ctx_t *ctx, char c);

/*
 * vfmt_parse_number - Parse numeric field width from format string
 *
 * Parses digits from the format string to build a field width value.
 * Returns -1 if no digits found.
 *
 * Original address: 0x00e6aa4c
 */
int16_t vfmt_parse_number(vfmt_ctx_t *ctx);

/*
 * vfmt_parse_number_after_m - Parse number after 'M' modifier
 *
 * Similar to vfmt_parse_number but starts parsing after an 'M'
 * modifier has been seen.
 *
 * Original address: 0x00e6aab6
 */
int16_t vfmt_parse_number_after_m(vfmt_ctx_t *ctx);

/*
 * vfmt_format_number - Format a numeric value
 *
 * Converts an integer to its string representation with the
 * specified base and formatting options.
 *
 * Parameters:
 *   spec       - Format specifier string
 *   value      - Pointer to value to format
 *   output     - Output buffer
 *   max_len    - Maximum output length
 *   out_len    - Receives actual output length
 *
 * Original address: 0x00e6a704
 */
void vfmt_format_number(const char *spec, void *value, char *output,
                        int16_t max_len, int16_t *out_len);

#endif /* VFMT_INTERNAL_H */
