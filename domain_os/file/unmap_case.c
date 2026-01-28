/*
 * UNMAP_CASE - Convert Domain/OS case-mapped pathname to Unix-style
 *
 * This function reverses the case mapping performed by MAP_CASE:
 *   - Bare UPPERCASE A-Z -> lowercase (was originally lowercase)
 *   - '\' -> '../' with preceding '/' separator if needed
 *   - ':' escape prefix -> decode next char(s):
 *       - ':' + uppercase letter -> keep uppercase (preserves original case)
 *       - ':' + lowercase letter -> uppercase it (robustness path)
 *       - ':' + digit 0-9 -> special char mapping:
 *           :0->! :1-># :2->% :3->& :4->+
 *           :5->- :6->? :7->= :8->@ :9->^
 *       - ':_' -> space
 *       - ':|' -> backslash
 *       - ':$' -> '$'
 *       - ':#XX' -> hex decode (two hex digits -> byte value)
 *       - ':' at end of input -> output ':' literally
 *       - ':' + other -> output char as-is
 *
 * At end: decrements out_len by 1 (internal 1-based -> external length),
 * null-terminates if room, sets truncated=0.
 *
 * Originally written in Pascal, compiled for m68k. The original assembly
 * uses bitmap lookup tables at 0xe543c6-0xe54410 for character classification
 * (uppercase, lowercase, digits, hex digits). These are Pascal compiler
 * optimizations for set membership tests. In C, we use equivalent range
 * checks for portability.
 *
 * Parameters:
 *   name        - Input pathname buffer (Domain/OS case-mapped)
 *   name_len    - Pointer to input length (int16_t)
 *   output      - Output buffer for Unix-style result
 *   max_out_len - Pointer to maximum output buffer size (int16_t)
 *   out_len     - Pointer to output length (int16_t, set on return)
 *   truncated   - Pointer to truncation flag (uint8_t, 0xFF if truncated, 0x00 if complete)
 *
 * Original address: 0x00e540d4
 * Size: 734 bytes
 */

#include "file/file.h"

/*
 * Helper: check if character is uppercase A-Z
 *
 * Replaces bitmap lookup table at DAT_00e543e0 which contains the
 * Pascal SET [A..Z] encoded as a bit array.
 */
static int is_upper(uint8_t ch)
{
    return (ch >= 'A' && ch <= 'Z');
}

/*
 * Helper: check if character is lowercase a-z
 *
 * Replaces bitmap lookup table at DAT_00e543f4 which contains the
 * Pascal SET [a..z] encoded as a bit array.
 */
static int is_lower(uint8_t ch)
{
    return (ch >= 'a' && ch <= 'z');
}

/*
 * Helper: check if character is a digit 0-9
 *
 * Replaces bitmap lookup table at DAT_00e543ec which contains the
 * Pascal SET [0..9] encoded as a bit array.
 */
static int is_digit(uint8_t ch)
{
    return (ch >= '0' && ch <= '9');
}

/*
 * Helper: check if character is uppercase letter A-Z (used for initial
 * char-after-colon test)
 *
 * Replaces bitmap lookup table at DAT_00e54404 which tests the same
 * set as DAT_00e543e0 but is referenced at a different PC-relative offset.
 */
static int is_upper_letter(uint8_t ch)
{
    return (ch >= 'A' && ch <= 'Z');
}

void UNMAP_CASE(char *name, int16_t *name_len, char *output,
                int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    int16_t in_len = *name_len;
    int16_t max_out = *max_out_len;
    uint8_t ch;
    uint8_t next_ch;

    /*
     * Original assembly uses 1-based indexing (Pascal convention):
     *   D1 = current input position (1-based)
     *   (A0) = output count (1-based, decremented by 1 at end)
     *
     * In C we track:
     *   ii = input index (0-based)
     *   oi = output position count (1-based, matching the assembly's behavior)
     *
     * Using 1-based oi here because the assembly's end-of-function logic
     * decrements by 1 and null-terminates at position oi (0-based),
     * so the final length = oi - 1.
     */
    int16_t ii;
    int16_t oi; /* 1-based output count, matching assembly */

    if (in_len == 0) {
        *truncated = 0;
        *out_len = 0;
        return;
    }

    *truncated = 0xFF;
    oi = 1;

    for (ii = 0; ii < in_len; ii++) {
        if (oi > max_out) {
            /* Output buffer full - truncated */
            *out_len = oi;
            return;
        }

        ch = (uint8_t)name[ii];

        /* Backslash: emit '../' with optional preceding '/' */
        if (ch == 0x5C) {
            /* If output is not empty and last char is not '/', prepend '/' */
            if (oi > 1 && output[oi - 2] != '/') {
                output[oi - 1] = 0x2F;  /* '/' */
                oi++;
            }

            /* Need room for '../' (3 chars but we only add 2 to oi since
             * the main loop increment handles 1) */
            if (oi + 2 > max_out) {
                *out_len = oi;
                return;
            }

            {
                int16_t pos = oi;
                output[pos - 1] = 0x2E;  /* '.' */
                output[pos]     = 0x2E;  /* '.' */
                output[pos + 1] = 0x2F;  /* '/' */
            }
            oi += 2;
            goto advance;
        }

        /* Check if character is uppercase A-Z (bare uppercase -> lowercase) */
        if (is_upper(ch)) {
            ch += 0x20;  /* Convert to lowercase */
            output[oi - 1] = ch;
            goto advance;
        }

        /* Colon escape prefix */
        if (ch == 0x3A) {
            /* Colon at end of input: output ':' literally */
            if (ii + 1 >= in_len) {
                output[oi - 1] = 0x3A;
                oi++;
                break;
            }

            /* Consume next character */
            ii++;
            next_ch = (uint8_t)name[ii];

            /*
             * Check if next char is an uppercase letter (DAT_00e54404 bitmap).
             * If so, it was originally uppercase - keep it as-is.
             */
            if (is_upper_letter(next_ch)) {
                ch = next_ch;
                output[oi - 1] = ch;
                goto advance;
            }

            /*
             * Check if next char is a lowercase letter (DAT_00e543f4 bitmap).
             * If so, convert to uppercase (robustness path).
             */
            if (is_lower(next_ch)) {
                ch = next_ch - 0x20;
                output[oi - 1] = ch;
                goto advance;
            }

            /*
             * Check if next char is a digit 0-9 (DAT_00e543ec bitmap).
             * Digits map to special characters.
             */
            if (is_digit(next_ch)) {
                /* Jump table for :0 through :9 */
                switch (next_ch) {
                case '0':
                    output[oi - 1] = 0x21;  /* '!' */
                    break;
                case '1':
                    output[oi - 1] = 0x23;  /* '#' */
                    break;
                case '2':
                    output[oi - 1] = 0x25;  /* '%' */
                    break;
                case '3':
                    output[oi - 1] = 0x26;  /* '&' */
                    break;
                case '4':
                    output[oi - 1] = 0x2B;  /* '+' */
                    break;
                case '5':
                    output[oi - 1] = 0x2D;  /* '-' */
                    break;
                case '6':
                    output[oi - 1] = 0x3F;  /* '?' */
                    break;
                case '7':
                    output[oi - 1] = 0x3D;  /* '=' */
                    break;
                case '8':
                    output[oi - 1] = 0x40;  /* '@' */
                    break;
                case '9':
                    output[oi - 1] = 0x5E;  /* '^' */
                    break;
                default:
                    /* Should not reach here since is_digit was true */
                    break;
                }
                goto advance;
            }

            /* Special escape sequences */
            if (next_ch == 0x5F) {
                /* ':_' -> space */
                output[oi - 1] = 0x20;
            } else if (next_ch == 0x7C) {
                /* ':|' -> backslash */
                output[oi - 1] = 0x5C;
            } else if (next_ch == 0x24) {
                /* ':$' -> '$' */
                output[oi - 1] = 0x24;
            } else if (next_ch == 0x23) {
                /*
                 * ':#XX' -> hex decode
                 *
                 * Original assembly at 0x00e542cc:
                 *   clr.w D0 (bVar2 = 0)
                 *   If at end of input, skip to advance
                 *   Otherwise consume up to 2 hex digits
                 */
                uint8_t value = 0;

                if (ii >= in_len - 1) {
                    /* '#' at end of colon-escape, no hex digits follow */
                    goto advance;
                }

                /* First hex digit */
                ii++;
                {
                    uint8_t h1 = (uint8_t)name[ii];
                    if (is_digit(h1)) {
                        value = h1 - 0x30;
                    } else if (is_lower(h1)) {
                        value = h1 - 0x61 + 10;
                    } else if (is_upper(h1)) {
                        value = h1 - 0x41 + 10;
                    } else {
                        /* Not a hex digit - output value as-is */
                        output[oi - 1] = value;
                        goto advance;
                    }
                }

                /* Second hex digit */
                if (ii >= in_len - 1) {
                    /* Only one hex digit available */
                    output[oi - 1] = value;
                    goto advance;
                }

                ii++;
                value <<= 4;
                {
                    uint8_t h2 = (uint8_t)name[ii];
                    if (is_digit(h2)) {
                        value = (h2 + value) - 0x30;
                    } else if (is_lower(h2)) {
                        value = (h2 + value) - 0x61 + 10;
                    } else if (is_upper(h2)) {
                        value = (h2 + value) - 0x41 + 10;
                    }
                    /* If not a hex digit, value stays as high nibble only */
                }

                output[oi - 1] = value;
            } else {
                /* ':' + other -> output the char as-is */
                output[oi - 1] = next_ch;
            }
            goto advance;
        }

        /* Default: output character unchanged */
        output[oi - 1] = ch;

    advance:
        /* In the assembly, sVar5 = sVar4 here (input position tracking).
         * In C, ii already tracks the consumed input position since we
         * increment ii directly when consuming multi-char escapes. */
        oi++;
    }

    /*
     * Post-processing (assembly at 0x00e543a6):
     *   subq.w #0x1,(A0)  - decrement output count (1-based -> length)
     *   null-terminate if room
     *   clear truncated flag
     */
    oi--;
    *out_len = oi;
    if (oi < max_out) {
        output[oi] = '\0';
    }
    *truncated = 0;
}
