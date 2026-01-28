/*
 * MAP_CASE - Convert Unix-style pathname to Domain/OS case-mapped representation
 *
 * This function maps Unix pathnames to Domain/OS case convention:
 *   - lowercase a-z -> uppercase (subtract 0x20)
 *   - UPPERCASE A-Z -> ':' + char (escape to preserve original case)
 *   - '.' at start of component:
 *       - '.' or '..' (followed by '/' or end) -> pass through
 *       - '.' followed by other chars -> ':' + '.'
 *   - '`' or '~' at start of component -> ':' + char
 *   - space -> ':_'
 *   - '\' -> ':|'
 *   - ':' -> '::' (escape the escape char)
 *   - control chars (1-31) and >= 0x7F -> ':#XX' (hex escape, lowercase hex digits)
 *   - '/' -> pass through, resets component start tracking
 *   - other printable -> pass through
 *
 * Originally written in Pascal, compiled for m68k. Uses 1-based indexing
 * internally (Pascal string convention). The C implementation uses 0-based
 * indexing but preserves the same semantics.
 *
 * Parameters:
 *   name       - Input pathname buffer
 *   name_len   - Pointer to input length (int16_t)
 *   output     - Output buffer for case-mapped result
 *   max_out_len - Pointer to maximum output buffer size (int16_t)
 *   out_len    - Pointer to output length (int16_t, set on return)
 *   truncated  - Pointer to truncation flag (uint8_t, 0xFF if truncated, 0x00 if complete)
 *
 * Original address: 0x00e53ef8
 * Size: 476 bytes
 */

#include "file/file.h"

void MAP_CASE(char *name, int16_t *name_len, char *output,
              int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    int16_t in_len = *name_len;
    int16_t max_out = *max_out_len;
    int16_t oi;  /* output index (0-based) */
    int16_t ii;  /* input index (0-based) */
    int16_t component_start; /* 0-based index of first char in current path component */
    uint8_t ch;

    *out_len = 0;

    if (in_len <= 0) {
        *truncated = 0;
        return;
    }

    *truncated = 0xFF;
    oi = 0;
    component_start = 0;

    /*
     * Original assembly uses 1-based indexing (Pascal convention):
     *   sVar5 (D2) = current input position (1-based)
     *   sVar6 (D4) = component start position (1-based)
     *   *param_5 (A0) = output position (1-based count)
     *
     * In this C version we use 0-based indexing throughout.
     */

    for (ii = 0; ii < in_len; ii++) {
        if (oi >= max_out) {
            /* Output buffer full - truncated */
            *out_len = oi;
            return;
        }

        ch = (uint8_t)name[ii];

        /* Check for special prefix characters at start of component */
        if (ii == component_start && (ch == 0x60 || ch == 0x2e || ch == 0x7e)) {
            if (ch != 0x2e) {
                /* Backtick (0x60) or tilde (0x7e) at start of component: escape with ':' */
                goto escape_with_colon;
            }

            /* Dot at start of component */
            if (ii + 1 >= in_len || name[ii + 1] == '/') {
                /* Single '.' at end or before '/' - pass through */
                oi++;
                *out_len = oi;
                output[oi - 1] = 0x2e;
                continue;
            }

            if (name[ii + 1] == '.' &&
                (ii + 2 >= in_len || name[ii + 2] == '/')) {
                /* '..' at end or before '/' - pass through the first dot */
                oi++;
                *out_len = oi;
                output[oi - 1] = ch;
                continue;
            }

            /* '.' followed by other chars - escape with ':' */
            goto escape_with_colon;
        }

        /* Uppercase A-Z: escape with ':' prefix */
        if (ch >= 0x41 && ch <= 0x5A) {
            goto emit_colon_char;
        }

        /* Lowercase a-z: convert to uppercase */
        if (ch >= 0x61 && ch <= 0x7A) {
            oi++;
            *out_len = oi;
            ch -= 0x20;
            output[oi - 1] = ch;
            continue;
        }

        /*
         * Control characters (0x01-0x1F) and high-bit characters (0x7F-0xFF):
         * encode as ':#XX' with lowercase hex digits.
         *
         * Original assembly:
         *   scc D1 (set if ch >= 1)
         *   sls D6 (set if ch <= 0x1F)
         *   and D6,D1 -> bmi (test bit 7 of result)
         * This tests: (ch >= 1 && ch <= 0x1F)
         * Then also: ch >= 0x7F (via separate compare, bhi to same target)
         * Note: 0xFF is included (the original bhi 0x00e54036 after cmpi.b #-0x1
         * would NOT branch for 0xFF, so 0xFF IS included in hex encoding)
         */
        if ((ch >= 0x01 && ch <= 0x1F) || ch >= 0x7F) {
            /* Need 4 bytes for ':#XX' */
            if (oi + 4 > max_out) {
                *out_len = oi;
                return;
            }
            oi += 4;
            *out_len = oi;
            output[oi - 4] = 0x3A;  /* ':' */
            output[oi - 3] = 0x23;  /* '#' */

            /*
             * High nibble: unconditionally adds 0x30.
             * Original assembly (0x00e54012): addi.b #0x30,D0b
             * This produces '0'-'9' for nibbles 0-9, and ':'-'?' for
             * nibbles 10-15. Not standard hex, but matches the original.
             */
            output[oi - 2] = (ch >> 4) + 0x30;

            /*
             * Low nibble: conditional - digits get 0x30, values >= 10 get 0x57
             * (producing lowercase hex 'a'-'f').
             * Original assembly (0x00e5401e-0x00e5402e):
             *   cmpi.w #0x9,D0w / bgt -> addi.b #0x57 / else addi.b #0x30
             */
            {
                uint8_t lo = ch & 0x0F;
                if (lo < 10) {
                    output[oi - 1] = lo + 0x30;
                } else {
                    output[oi - 1] = lo + 0x57;
                }
            }
            continue;
        }

        /* Colon: escape as '::' */
        if (ch == 0x3A) {
            goto escape_with_colon;
        }

        /* Space: escape as ':_' */
        if (ch == 0x20) {
            if (oi + 2 > max_out) {
                *out_len = oi;
                return;
            }
            oi += 2;
            *out_len = oi;
            output[oi - 2] = 0x3A;  /* ':' */
            output[oi - 1] = 0x5F;  /* '_' */
            continue;
        }

        /* Backslash: escape as ':|' */
        if (ch == 0x5C) {
            if (oi + 2 > max_out) {
                *out_len = oi;
                return;
            }
            oi += 2;
            *out_len = oi;
            output[oi - 2] = 0x3A;  /* ':' */
            output[oi - 1] = 0x7C;  /* '|' */
            continue;
        }

        /* All other printable characters: pass through */
        oi++;
        *out_len = oi;
        output[oi - 1] = ch;

        /* '/' resets the component start tracker */
        if (ch == 0x2F) {
            component_start = ii + 1;
        }
        continue;

    escape_with_colon:
        /* Emit ':' + char, checking buffer space first */
        if (oi + 2 > max_out) {
            *out_len = oi;
            return;
        }
    emit_colon_char:
        oi += 2;
        *out_len = oi;
        output[oi - 2] = 0x3A;  /* ':' */
        output[oi - 1] = ch;
        continue;
    }

    *truncated = 0;
}
