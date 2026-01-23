/*
 * vfmt/main.c - VFMT_$MAIN implementation
 *
 * Main format string processor for the Domain/OS kernel.
 * Provides printf-like formatting with Domain/OS-specific extensions.
 *
 * This is a complex Pascal-derived function with several nested
 * sub-procedures that have been translated to static helper functions.
 *
 * Original address: 0x00e6ab2a
 *
 * Format specifiers:
 *   %a   - ASCII string with length
 *   %d   - Decimal integer (long)
 *   %h   - Hexadecimal integer
 *   %o   - Octal integer
 *   %wd  - Word (16-bit) decimal
 *   %wh  - Word (16-bit) hexadecimal
 *   %t   - Tab to position
 *   %x   - Repeat character
 *   %(n) - Repeat group n times
 *   %)   - End repeat group
 *   %%   - Literal percent
 *   %$   - End of format
 *   %/   - Flush output
 */

#include "vfmt/vfmt_internal.h"

/* Math helper functions from m68k library */
extern int32_t M_DIU_LLW(int32_t dividend, int16_t divisor);
extern int16_t M_OIU_WLW(int32_t dividend, int16_t divisor);

/*
 * Static helper functions (nested Pascal sub-procedures)
 */

/*
 * output_char - Output a single character to the buffer
 *
 * Implements VFMT_$MAIN_FUN_00e6a9f6
 * Accesses parent frame via unaff_A6 in original code.
 */
static void output_char(vfmt_ctx_t *ctx, char c)
{
    int16_t pos = *ctx->out_len_p;

    if (pos < ctx->max_len) {
        (*ctx->out_len_p)++;
        ctx->output[pos] = c;
    }
}

/*
 * parse_width - Parse numeric width from format specifier
 *
 * Implements VFMT_$MAIN_FUN_00e6aa4c
 * Parses digits to form a width value, returns -1 if no digits.
 */
static int16_t parse_width(const char *spec, int16_t spec_len)
{
    int16_t result = -1;
    int16_t i;
    uint8_t c;

    for (i = 0; i < spec_len; i++) {
        c = spec[i];

        /* Stop if we see 'M' modifier */
        if (c == VFMT_MOD_M) {
            return result;
        }

        /* Parse digits */
        if (c >= '0' && c <= '9') {
            if (result == -1) {
                result = c - '0';
            } else {
                result = result * 10 + (c - '0');
            }
        }
    }

    return result;
}

/*
 * parse_width_after_m - Parse width after 'M' modifier
 *
 * Implements VFMT_$MAIN_FUN_00e6aab6
 * Only parses digits that appear after an 'M' in the specifier.
 */
static int16_t parse_width_after_m(const char *spec, int16_t spec_len)
{
    int16_t result = -1;
    int16_t i;
    uint8_t c;
    int seen_m = 0;

    for (i = 0; i < spec_len; i++) {
        c = spec[i];

        if (c == VFMT_MOD_M) {
            seen_m = 1;
            continue;
        }

        if (seen_m && c >= '0' && c <= '9') {
            if (result == -1) {
                result = c - '0';
            } else {
                result = result * 10 + (c - '0');
            }
        }
    }

    return result;
}

/*
 * get_next_arg - Get next argument from varargs list
 *
 * Implements FUN_00e6aa38
 * Advances the argument pointer and returns the value.
 */
static void *get_next_arg(vfmt_ctx_t *ctx)
{
    void *arg = *ctx->args;
    ctx->args++;
    ctx->arg_count++;
    return arg;
}

/*
 * format_number - Format a numeric value
 *
 * Implements FUN_00e6a704
 * Converts integer to string with specified base and options.
 */
static void format_number(const char *spec, int16_t spec_len, void *value_p,
                          char *output, int16_t max_len, int16_t *out_len)
{
    char digits[20];
    int32_t value;
    uint16_t base = 10;
    int is_word = 0;       /* W modifier: 16-bit value */
    int is_signed = 0;     /* S modifier: signed */
    int show_plus = 0;     /* P modifier: show + for positive */
    int zero_strip = 1;    /* Default: strip leading zeros */
    int left_justify = 0;  /* J modifier: no leading zeros */
    int16_t width = -1;
    int16_t i, j;
    uint8_t c;
    int is_negative = 0;
    int16_t digit_count;
    int16_t pad_count;

    /* Parse format specifier */
    for (i = 0; i < spec_len; i++) {
        c = spec[i];

        /* Convert to uppercase */
        if (c >= 'a' && c <= 'z') {
            c -= 0x20;
        }

        switch (c) {
        case 'D':
            base = 10;
            break;
        case 'H':
            base = 16;
            break;
        case 'O':
            base = 8;
            break;
        case 'W':
            is_word = 1;
            break;
        case 'S':
            is_signed = 1;
            break;
        case 'U':
            is_signed = 0;
            break;
        case 'P':
            show_plus = 1;
            break;
        case 'Z':
            zero_strip = 0;
            break;
        case 'L':
            is_word = 1;  /* L = long, but treat as default */
            break;
        case 'J':
            left_justify = 1;
            break;
        case 'R':
            left_justify = 0;
            break;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            if (width == -1) {
                width = c - '0';
            } else {
                width = width * 10 + (c - '0');
            }
            break;
        case ' ':
            /* End of specifier */
            break;
        default:
            /* Unknown character - treat as error */
            output[0] = '?';
            *out_len = 1;
            return;
        }
    }

    /* Get value based on size */
    if (is_word) {
        if (is_signed) {
            value = *(int16_t *)value_p;
            if (value < 0) {
                value = -value;
                is_negative = 1;
            }
        } else {
            value = *(uint16_t *)value_p;
        }
    } else {
        value = *(int32_t *)value_p;
        if (is_signed && value < 0) {
            value = -value;
            is_negative = 1;
        }
    }

    /* Convert to string (reverse order) */
    digit_count = 0;
    do {
        int16_t digit = M_OIU_WLW(value, base);
        if (digit < 10) {
            digits[19 - digit_count] = '0' + digit;
        } else {
            digits[19 - digit_count] = 'A' + digit - 10;
        }
        value = M_DIU_LLW(value, base);
        digit_count++;
    } while (value != 0 && digit_count < 20);

    /* Handle zero-stripping */
    if (zero_strip) {
        while (digit_count > 1 && digits[20 - digit_count] == '0') {
            digits[20 - digit_count] = ' ';
            /* Don't actually reduce digit_count, just pad with spaces */
        }
    }

    /* Calculate padding */
    if (width > digit_count) {
        pad_count = width - digit_count;
        if (is_negative || show_plus) {
            pad_count--;
        }
    } else {
        pad_count = 0;
    }

    /* Output the result */
    j = 0;

    /* Add sign if needed */
    if (is_negative || show_plus) {
        if (!left_justify && width > digit_count + 1) {
            /* Insert sign at correct position for right justify */
            j = 0;
        }
        if (is_negative) {
            digits[19 - digit_count] = '-';
        } else if (show_plus) {
            digits[19 - digit_count] = '+';
        }
        digit_count++;
    }

    /* Pad if needed */
    if (!left_justify) {
        for (i = 0; i < pad_count && j < max_len; i++, j++) {
            output[j] = ' ';
        }
    }

    /* Output digits */
    for (i = 0; i < digit_count && j < max_len; i++, j++) {
        output[j] = digits[20 - digit_count + i];
    }

    /* Right pad for left justify */
    if (left_justify) {
        for (i = 0; i < pad_count && j < max_len; i++, j++) {
            output[j] = ' ';
        }
    }

    *out_len = j;
}

/*
 * VFMT_$MAIN - Main format string processor
 *
 * Processes a format string with arguments and produces formatted output.
 */
void VFMT_$MAIN(const char *format, char *buf, int16_t *max_len,
                int16_t *out_len, void *args)
{
    vfmt_ctx_t ctx;
    int16_t format_pos = 0;
    int16_t spec_len;
    char spec[12];
    uint8_t c;
    int16_t i;
    int16_t width, max_width;
    int16_t repeat_count = 0;
    int16_t repeat_pos = 0;
    int16_t max_written = 0;
    int uppercase = 0;
    int lowercase = 0;
    int zero_strip = 0;

    /* Initialize context */
    ctx.format = format;
    ctx.output = buf;
    ctx.max_len = *max_len;
    ctx.out_len_p = out_len;
    ctx.args = (void **)args;
    ctx.arg_count = 0;

    *out_len = 0;

    /* Process format string (1-based indexing in original) */
    while (format_pos < 200) {  /* Max format length */
        format_pos++;
        c = format[format_pos - 1];

        /* Regular character - just output it */
        if (c != '%') {
            output_char(&ctx, c);
            continue;
        }

        /* Parse format specifier */
        spec_len = 0;
        do {
            format_pos++;
            c = format[format_pos - 1];

            /* Convert to uppercase for comparison */
            if (c >= 'a' && c <= 'z') {
                c -= 0x20;
            }

            /* Check for terminating characters */
            if (c == '%' || c == 'T' || c == 'A' || c == ')' ||
                c == '(' || c == 'X' || c == 'H' || c == 'D' ||
                c == 'O' || c == '/' || c == '.' || c == '$') {
                break;
            }

            /* Skip spaces but don't add to spec */
            if (c != ' ') {
                spec_len++;
                if (spec_len <= 10) {
                    spec[spec_len - 1] = c;
                }
            }
        } while (spec_len <= 10);

        if (spec_len > 10) {
            /* Format spec too long - error */
            output_char(&ctx, '?');
            output_char(&ctx, '?');
            return;
        }

        /* Handle '.' or '/' - flush/end */
        if (c == '.' || c == '/') {
            /* Flush output */
            continue;
        }

        /* Handle '$' or '.' - end of format */
        if (c == '$' || c == '.') {
            if (max_written > *out_len) {
                *out_len = max_written;
            }
            return;
        }

        /* Handle specific format types */
        switch (c) {
        case 'H':
        case 'O':
        case 'D':
            /* Numeric format */
            {
                void *value_p;
                int16_t num_len;
                int16_t remaining;

                spec[spec_len] = c;
                spec_len++;
                spec[spec_len] = ' ';
                spec_len++;

                (*out_len)++;
                ctx.arg_count++;
                value_p = get_next_arg(&ctx);

                remaining = ctx.max_len - *out_len + 1;
                format_number(spec, spec_len, value_p,
                              buf + *out_len - 1, remaining, &num_len);
                *out_len = *out_len + num_len - 1;
            }
            break;

        case 'A':
            /* ASCII string */
            {
                char *str_p;
                int16_t str_len;
                int16_t max_str_len;

                /* Get width and max width from spec */
                width = parse_width(spec, spec_len);
                max_width = parse_width_after_m(spec, spec_len);

                if (max_width < 0) {
                    /* Length from argument */
                    ctx.arg_count += 2;
                    str_p = (char *)get_next_arg(&ctx);
                    str_len = *(int16_t *)get_next_arg(&ctx);
                } else {
                    ctx.arg_count++;
                    str_len = max_width;
                    str_p = (char *)get_next_arg(&ctx);
                }

                /* Check for U (uppercase) and L (lowercase) modifiers */
                for (i = 0; i < spec_len; i++) {
                    if (spec[i] == 'U') uppercase = 1;
                    else if (spec[i] == 'L') lowercase = 1;
                    else if (spec[i] == 'Z') zero_strip = 1;
                }

                /* Limit string length if width specified */
                if (width >= 0 && width < str_len) {
                    str_len = width;
                }

                /* Output the string */
                for (i = 0; i < str_len; i++) {
                    char ch = str_p[i];

                    /* Apply case conversion */
                    if (uppercase && ch >= 'a' && ch <= 'z') {
                        ch -= 0x20;
                    } else if (lowercase && ch >= 'A' && ch <= 'Z') {
                        ch += 0x20;
                    }

                    output_char(&ctx, ch);
                }

                /* Pad if width > string length */
                if (width > str_len) {
                    for (i = str_len; i < width; i++) {
                        output_char(&ctx, ' ');
                    }
                }
            }
            break;

        case '(':
            /* Start repeat group */
            {
                int16_t count = parse_width(spec, spec_len);
                if (count < 1) count = 1;
                repeat_count = count - 1;
                repeat_pos = format_pos;
            }
            break;

        case ')':
            /* End repeat group */
            if (repeat_count > 0) {
                repeat_count--;
                format_pos = repeat_pos;
            }
            break;

        case '%':
            /* Literal percent */
            output_char(&ctx, '%');
            break;

        case 'T':
            /* Tab to position */
            {
                int16_t tab_pos;

                width = parse_width(spec, spec_len);
                if (width < 0) {
                    /* Position from argument */
                    int16_t *pos_p;
                    ctx.arg_count++;
                    pos_p = (int16_t *)get_next_arg(&ctx);
                    tab_pos = *pos_p;
                    if (tab_pos < 1) {
                        /* Get from next arg */
                        ctx.arg_count++;
                        pos_p = (int16_t *)get_next_arg(&ctx);
                        tab_pos = *pos_p;
                    }
                } else {
                    tab_pos = width;
                }

                if (tab_pos <= 0 || tab_pos > 1023) {
                    tab_pos = *out_len + 1;
                }

                /* Track max position */
                if (*out_len > max_written) {
                    max_written = *out_len;
                }

                /* Fill with spaces up to tab position */
                if (tab_pos > max_written + 1) {
                    for (i = max_written; i < tab_pos - 1; i++) {
                        buf[i] = ' ';
                    }
                }

                *out_len = tab_pos - 1;
            }
            break;

        case 'X':
            /* Repeat spaces */
            {
                int16_t count = parse_width(spec, spec_len);
                if (count < 1) count = 1;
                for (i = 0; i < count; i++) {
                    output_char(&ctx, ' ');
                }
            }
            break;
        }
    }
}
