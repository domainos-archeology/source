/*
 * vfmt/main.c - VFMT_$MAIN, the Domain/OS kernel formatter
 *
 * Original address: 0x00E6AB2A (SAU2 map: VFMT_ code segment)
 * Size: 1200 bytes (0x00E6AB2A .. 0x00E6AFD9); its four constant cells
 * follow at 0x00E6AFDA .. 0x00E6AFE1.
 *
 * Five nested Pascal procedures precede it in the image and reach its
 * frame through the static link (`movea.l (A6),A0`):
 *   vfmt_format_number         0x00E6A704  532 bytes
 *   vfmt_output_char           0x00E6A9F6   66 bytes
 *   vfmt_get_next_arg          0x00E6AA38   20 bytes
 *   vfmt_parse_number          0x00E6AA4C  106 bytes
 *   vfmt_parse_number_after_m  0x00E6AAB6  116 bytes
 * They are static functions here, taking the frame explicitly.
 *
 * Format language (the letter after '%', case-folded, with an optional
 * modifier string of up to ten characters between them):
 *   %D %H %O   number in base 10 / 16 / 8 from the next argument (a
 *              pointer to the value); modifiers W word, L long, S signed,
 *              U unsigned, P signed with '+', Z keep leading zeros,
 *              J left-justify, R right-justify, digits = field width
 *   %A         string: next argument is the text pointer, the one after
 *              it a pointer to the length word (or, with an M<n>
 *              modifier, the length is n and only the pointer is taken);
 *              digits = maximum length, U / L fold case; without Z
 *              (and without a width) trailing blanks are dropped
 *   %T         tab to column (digits, or a word argument)
 *   %X         digits blanks
 *   %(  %)     repeat group (digits = count)
 *   %%         a '%'
 *   %/         newline
 *   %.         newline, then end;  %$  end
 * A modifier string longer than ten characters, or 200 characters of
 * format without an end marker, emits "??" and stops.
 *
 * VFMT_$MAIN frame (link.w A6,-0x48; D2-D7/A2-A4 saved):
 *   (0x8,A6)   format     pointer, 1-based indexing
 *   (0xc,A6)   buf        output pointer
 *   (0x10,A6)  max_len    pointer to a word (re-read at every use)
 *   (0x14,A6)  out_len    pointer to a word, the running output length
 *   (0x18,A6)  args       pointer to the caller's argument list (each
 *                         entry a longword pointer)
 *   (-0x10,A6) spec[10]   the modifier string, written 1-based as
 *                         (-0x11,A6,i)
 *   (-0x1c,A6) tmp_ptr    scratch cell for the by-reference get_next_arg
 *   (-0x1e,A6) repeat_pos (-0x20,A6) repeat_count
 *   (-0x22,A6) max_written  the highest %T column seen
 *   (-0x24,A6) width      (-0x26,A6) width_after_m
 *   (-0x2c,A6) num_len    vfmt_format_number's output length
 *   (-0x2e,A6) pos        the format position (1-based)
 *   (-0x30,A6) spec_len
 *   (-0x32,A6) arg_count
 *   (-0x34,A6) x_count
 *   (-0x3a,A6) ch         the byte handed to output_char by reference
 *   (-0x3e,A6) lowercase  (-0x40,A6) uppercase   Domain booleans
 *   (-0x48,A6) args cursor (kept in step with A3 and A4)
 *
 * Re-emitted from the disassembly 2026-09-27: the previous C was a loose
 * paraphrase (a different number formatter, %A/%T argument handling,
 * trailing-blank dropping missing, '.' / '/' emitting nothing, no "??"
 * on the 200-character limit).
 */

#include "vfmt/vfmt_internal.h"
#include "math/math.h"

/* Constant cells in the code region, passed by address to output_char. */
static const char vfmt_$main_newline_00e6afda = 0x0a;  /* `pea (0x3b8,PC)` 0x00E6AC20 */
static const char vfmt_$main_query_00e6afdc = '?';     /* `pea (0x1c,PC)` / `pea (0x12,PC)` 0x00E6AFBE */
static const char vfmt_$main_percent_00e6afde = '%';   /* `pea (0x10c,PC)` 0x00E6AED0 */
static const char vfmt_$main_blank_00e6afe0 = ' ';     /* `pea (0x1b6,PC)` 0x00E6AE28, `pea (0x3a,PC)` 0x00E6AFA4 */

/* VFMT_$MAIN's frame as the nested procedures see it. */
typedef struct vfmt_$frame_t {
    const char *format;         /* (0x8,A6) */
    char *buf;                  /* (0xc,A6) */
    const int16_t *max_len;     /* (0x10,A6) */
    int16_t *out_len;           /* (0x14,A6) */
    char spec[10];              /* (-0x10,A6) */
    int16_t spec_len;           /* (-0x30,A6) */
} vfmt_$frame_t;

/*
 * vfmt_output_char (0x00E6A9F6) - append one character
 *
 *   0x00E6AA04  D0w = *out_len; cmp.w (*max_len),D0w; bge -> nothing
 *   0x00E6AA12  (*out_len)++; buf[*out_len - 1] = *ch
 */
static void vfmt_output_char(vfmt_$frame_t *f, const char *ch)
{
    int16_t pos = *f->out_len;

    if (pos >= *f->max_len) {
        return;
    }
    *f->out_len = (int16_t)(pos + 1);
    f->buf[*f->out_len - 1] = *ch;
}

/*
 * vfmt_get_next_arg (0x00E6AA38) - fetch the longword a cell points at
 *
 * Takes the ADDRESS of a cell holding a pointer and returns that pointer
 * (A0); with the cell being an argument-list slot this yields the
 * argument, with the cell being (-0x1c,A6) it yields the cell's content.
 */
static void *vfmt_get_next_arg(void **cell)
{
    return *cell;
}

/*
 * vfmt_parse_number (0x00E6AA4C) - the decimal digits of the modifier
 * string up to the first 'M'; -1 when there are none.
 *
 *   0x00E6AA5A  *out = -1
 *   0x00E6AA5E  spec_len - 1 as a dbf count; i = 1 ..
 *   0x00E6AA68    c = spec[i-1]; 'M' -> stop; not '0'..'9' -> next
 *   0x00E6AA7E    *out == -1 ? *out = c - '0' : *out = *out * 10 + c - '0'
 */
static void vfmt_parse_number(const vfmt_$frame_t *f, int16_t *out)
{
    int16_t count;
    int16_t i;
    uint8_t c;

    *out = -1;
    count = (int16_t)(f->spec_len - 1);
    if (count < 0) {
        return;
    }
    for (i = 1; count != -1; count--, i++) {
        c = (uint8_t)f->spec[i - 1];
        if (c == VFMT_MOD_M) {
            return;
        }
        if (c < '0' || c > '9') {
            continue;
        }
        if (*out == -1) {
            *out = (int16_t)(c - '0');
        } else {
            *out = (int16_t)(*out * 10 + c - '0');
        }
    }
}

/*
 * vfmt_parse_number_after_m (0x00E6AAB6) - the decimal digits that follow
 * an 'M' in the modifier string; -1 when there are none.
 *
 *   0x00E6AAC8  seen = 0
 *   0x00E6AAD4    c = spec[i-1]; 'M' -> seen = true, next
 *   0x00E6AAE2    scc(c >= '0') & seen must be negative, c <= '9'
 *   0x00E6AAF2    accumulate as above
 */
static void vfmt_parse_number_after_m(const vfmt_$frame_t *f, int16_t *out)
{
    int16_t count;
    int16_t i;
    uint8_t c;
    boolean seen = false;

    *out = -1;
    count = (int16_t)(f->spec_len - 1);
    if (count < 0) {
        return;
    }
    for (i = 1; count != -1; count--, i++) {
        c = (uint8_t)f->spec[i - 1];
        if (c == VFMT_MOD_M) {
            seen = true;
            continue;
        }
        if (!(c >= '0' && seen < 0)) {
            continue;
        }
        if (c > '9') {
            continue;
        }
        if (*out == -1) {
            *out = (int16_t)(c - '0');
        } else {
            *out = (int16_t)(*out * 10 + c - '0');
        }
    }
}

/*
 * vfmt_format_number (0x00E6A704) - render one number
 *
 * Frame (link.w A6,-0x44; D2-D7/A2 saved):
 *   (0x8,A6)   spec       pointer -> A0, scanned up to ten characters
 *   (0xc,A6)   value_p    pointer to the value
 *   (0x10,A6)  output     pointer -> A2
 *   (0x14,A6)  max_len    WORD, by value
 *   (0x16,A6)  out_len    pointer -> A1
 *   (-0x18,A6) digits[20] the number, least significant digit last
 *   (-0x2e,A6) size       1 = long (default, 'L'), 0 = word ('W')
 *   (-0x32,A6) base       10 (default, 'D'), 16 ('H'), 8 ('O')
 *   (-0x34,A6) sign_mode  0 unsigned ('U'), 1 signed ('S'), 2 signed
 *                         with '+' ('P')
 *   (-0x3a,A6) strip      Domain boolean, true unless 'Z'
 *   (-0x3c,A6) bad        Domain boolean, any other character
 *   (-0x3e,A6) right      Domain boolean, true unless 'J'; 'R' sets it
 *   D4w        width      -1 unless digits are given
 *
 * The specifier loop (0x00E6A736 .. 0x00E6A84E) folds each character to
 * upper case, subtracts 0x20 and dispatches through the 0x3B-entry jump
 * table at 0x00E6A76A: ' ' ends the scan, the letters above set their
 * flag, '0'..'9' build the width, anything else sets `bad`.
 */
static void vfmt_format_number(const char *spec, const void *value_p, char *output,
                               int16_t max_len, int16_t *out_len)
{
    char digits[20];            /* (-0x18,A6) */
    int16_t size = 1;           /* (-0x2e,A6) */
    int16_t base = 10;          /* (-0x32,A6) */
    int16_t sign_mode = 0;      /* (-0x34,A6) */
    boolean strip = true;       /* (-0x3a,A6) */
    boolean bad = false;        /* (-0x3c,A6) */
    boolean right = true;       /* (-0x3e,A6) */
    int16_t width = -1;         /* D4w */
    boolean negative;           /* D5b */
    uint32_t value;             /* D3 */
    int16_t count;              /* D2w after the strip loop */
    int16_t start;              /* D1w, the first digits[] index - 1 */
    int16_t sign_pos;           /* D3w */
    int16_t d0, d1, d2, k;
    uint8_t c;

    /* 0x00E6A736 .. 0x00E6A84E: up to ten specifier characters */
    for (d1 = 9, k = 0; d1 != -1; d1--, k++) {
        c = (uint8_t)spec[k];
        if (c >= 'a' && c <= 'z') {
            c = (uint8_t)(c - 0x20);
        }
        if ((uint16_t)(c - 0x20) >= 0x3b) {
            bad = true;                                     /* 0x00E6A848 */
            continue;
        }
        switch (c) {
        case ' ':  goto scanned;                            /* 0x00E6A852 */
        case 'S':  sign_mode = 1; break;                    /* 0x00E6A7E0 */
        case 'U':  sign_mode = 0; break;                    /* 0x00E6A7E8 */
        case 'P':  sign_mode = 2; break;                    /* 0x00E6A7EE */
        case 'O':  base = 8;  break;                        /* 0x00E6A7F6 */
        case 'D':  base = 10; break;                        /* 0x00E6A7FE */
        case 'H':  base = 16; break;                        /* 0x00E6A806 */
        case 'Z':  strip = false; break;                    /* 0x00E6A80E */
        case 'W':  size = 0; break;                         /* 0x00E6A814 */
        case 'L':  size = 1; break;                         /* 0x00E6A81A */
        case 'R':  right = true; break;                     /* 0x00E6A822 */
        case 'J':  right = false; break;                    /* 0x00E6A828 */
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            /* 0x00E6A82E .. 0x00E6A846 */
            if (width == -1) {
                width = (int16_t)(c - '0');
            } else {
                width = (int16_t)(width * 10 + c - '0');
            }
            break;
        default:
            bad = true;                                     /* 0x00E6A848 */
            break;
        }
    }
scanned:
    /* 0x00E6A852 .. 0x00E6A860 */
    if (bad < 0) {
        output[0] = '?';
        *out_len = 1;
        return;
    }

    /* 0x00E6A864 .. 0x00E6A8B4: fetch the value, a word or a longword,
     * and take its magnitude when signed */
    negative = false;
    value = 0;
    if (size == 0) {
        if (sign_mode != 0) {
            int16_t w = *(const int16_t *)value_p;
            if (w < 0) {
                value = (uint32_t)(int32_t)(int16_t)(0 - w);    /* neg.w / ext.l */
                negative = true;
            } else {
                value = (uint32_t)(int32_t)w;
            }
        } else {
            value = *(const uint16_t *)value_p;
        }
    } else if (size == 1) {
        value = *(const uint32_t *)value_p;
        if (sign_mode != 0) {
            if ((int32_t)value < 0) {
                value = 0u - value;
                negative = true;
            }
        }
    }

    /* 0x00E6A8B6 .. 0x00E6A8F6: all twenty digits, last first */
    for (d2 = 0x13, d1 = 0x13; d2 != -1; d2--, d1--) {
        uint8_t digit = (uint8_t)(M$OIU$WLW((long)value, (short)base) + 0x30);
        digits[d1] = (char)digit;
        if (digit > '9') {
            digits[d1] = (char)(digit + 7);
        }
        value = (uint32_t)M$DIU$LLW(value, (ushort)base);
    }

    /* 0x00E6A8FA .. 0x00E6A918: count the significant digits, blanking
     * the leading zeros when stripping; at least one digit stays */
    count = 0x14;
    for (d1 = 0x12, d0 = 0; d1 != -1; d1--, d0++) {
        if (digits[d0] != '0') {
            break;
        }
        if (strip < 0) {
            digits[d0] = ' ';
        }
        count--;
    }

    /* 0x00E6A91C .. 0x00E6A958: the sign, when negative or 'P' */
    if (negative < 0 || sign_mode == 2) {
        count++;
        if (strip < 0 || (int32_t)width <= (int32_t)count) {
            sign_pos = (int16_t)(0x14 - count);             /* 0x00E6A93C */
        } else {
            sign_pos = (int16_t)(0x14 - width);             /* 0x00E6A942 */
        }
        digits[sign_pos] = (negative < 0) ? '-' : '+';
    }

    /* 0x00E6A95A .. 0x00E6A966: the field is at least `count` wide */
    if ((int32_t)count > (int32_t)width) {
        width = count;
    }

    /* 0x00E6A968 .. 0x00E6A982: blank the field */
    d1 = (int16_t)(width - 1);
    if (d1 >= 0) {
        for (d0 = 0; d1 != -1; d1--, d0++) {
            if (d0 < max_len) {
                output[d0] = ' ';
            }
        }
    }

    /* 0x00E6A986 .. 0x00E6A9A6 */
    *out_len = 0;
    if ((int8_t)(~strip | right) < 0 || (int32_t)width <= (int32_t)count) {
        start = (int16_t)(0x13 - width);                    /* 0x00E6A99C */
    } else {
        start = (int16_t)(0x13 - count);                    /* 0x00E6A9A2 */
    }

    /* 0x00E6A9A8 .. 0x00E6A9E8: `width` characters from digits[start+1..].
     * `clr.l D3` / `move.w D1w,D3w` zero-extends the start, so a negative
     * start (a width above 19) makes every index "out of range" and the
     * whole field comes out blank; the digits index itself is the word
     * sum (`move.w D4w,D6w` / `add.w D1w,D6w`). */
    d1 = (int16_t)(width - 1);
    if (d1 < 0) {
        return;
    }
    for (k = 1; d1 != -1; d1--, k++) {
        int16_t pos = *out_len;
        int32_t idx = (int32_t)k + (int32_t)(uint16_t)start;
        if (pos >= max_len) {
            return;
        }
        if (idx > 0x13 || idx < 0) {
            output[pos] = ' ';
        } else {
            output[pos] = digits[(int16_t)(k + start)];
        }
        (*out_len)++;
    }
}

void VFMT_$MAIN(const char *format, char *buf, const int16_t *max_len,
                int16_t *out_len, void *args)
{
    vfmt_$frame_t f;
    void **cursor = (void **)args;      /* A3 == A4 == (-0x48,A6) */
    void *tmp_ptr;                      /* (-0x1c,A6) */
    int16_t arg_count = 0;              /* (-0x32,A6) */
    int16_t pos = 0;                    /* (-0x2e,A6) */
    int16_t max_written = 0;            /* (-0x22,A6) */
    int16_t repeat_count = 0;           /* (-0x20,A6) */
    int16_t repeat_pos = 0;             /* (-0x1e,A6) */
    int16_t width;                      /* (-0x24,A6) / D6w */
    int16_t len;                        /* (-0x26,A6) / D7w */
    int16_t num_len;                    /* (-0x2c,A6) */
    int16_t x_count;                    /* (-0x34,A6) */
    char ch;                            /* (-0x3a,A6) */
    boolean uppercase, lowercase;       /* (-0x40,A6), (-0x3e,A6) */
    boolean keep_blanks;                /* D2b, the Z modifier */
    uint8_t c;                          /* D5b */
    boolean done;                       /* D0b */
    const char *str;                    /* A2 */
    int16_t *word_p;                    /* A2 */
    int16_t d0, d1, d2, d3, d4, k;

    f.format = format;
    f.buf = buf;
    f.max_len = max_len;
    f.out_len = out_len;
    f.spec_len = 0;
    *out_len = 0;                                           /* 0x00E6AB42 */

    /* 0x00E6AFB4: `cmpi.w #0xc8,(-0x2e,A6)` / `blt` */
    while (pos < 0xc8) {
        pos++;                                              /* 0x00E6AB5A */
        if (format[pos - 1] != '%') {
            vfmt_output_char(&f, &format[pos - 1]);         /* 0x00E6AB6E */
            continue;
        }

        /* 0x00E6AB76 .. 0x00E6AC06: gather the modifier string; the
         * terminating letter is stored too, blanks are skipped */
        f.spec_len = 0;
        done = false;
        c = 0;
        do {
            pos++;
            c = (uint8_t)format[pos - 1];
            if (c >= 'a' && c <= 'z') {
                c = (uint8_t)(c - 0x20);
            }
            if (c == '%' || c == 'T' || c == 'A' || c == ')' || c == '(' ||
                c == 'X' || c == 'H' || c == 'D' || c == 'O' || c == '/' ||
                c == '.' || c == '$') {
                done = true;
            }
            if (c != ' ') {
                f.spec_len++;
                if (f.spec_len > 10) {
                    break;                                  /* 0x00E6ABFA bgt */
                }
                f.spec[f.spec_len - 1] = (char)c;
            }
        } while (done >= 0);

        /* 0x00E6AC0A: more than ten modifier characters -> "??" and out */
        if (f.spec_len > 10) {
            goto too_long;
        }

        /* 0x00E6AC14 .. 0x00E6AC28: '.' and '/' emit a newline */
        if (c == '.' || c == '/') {
            vfmt_output_char(&f, &vfmt_$main_newline_00e6afda);
        }

        /* 0x00E6AC2A .. 0x00E6AC46: '.' and '$' end the format */
        if (c == '.' || c == '$') {
            if (max_written > *out_len) {
                *out_len = max_written;
            }
            return;
        }

        if (c == 'H' || c == 'O' || c == 'D') {
            /* 0x00E6AC5C .. 0x00E6AC6C: append a blank to the modifiers */
            if (f.spec_len < 10) {
                f.spec_len++;
                f.spec[f.spec_len - 1] = ' ';
            }
            /* 0x00E6AC72 .. 0x00E6AC7E */
            (*out_len)++;
            arg_count++;
            /* 0x00E6AC82 .. 0x00E6ACAC: format_number(spec, *cursor++,
             * buf + *out_len - 1, *max_len - *out_len + 1, &num_len) */
            vfmt_format_number(f.spec, *cursor, buf + *out_len - 1,
                               (int16_t)(*max_len - *out_len + 1), &num_len);
            cursor++;
            /* 0x00E6ACB0 .. 0x00E6ACBC */
            *out_len = (int16_t)(*out_len + num_len - 1);
            continue;
        }

        if (c == 'A') {
            /* 0x00E6ACCA .. 0x00E6ACE2 */
            vfmt_parse_number(&f, &width);
            vfmt_parse_number_after_m(&f, &len);
            if (len < 0) {
                /* 0x00E6ACE8 .. 0x00E6AD1E: two arguments - the text and a
                 * pointer to its length word.  A zero or negative length
                 * word means the length is the word after it (the low
                 * half of a longword length). */
                arg_count = (int16_t)(arg_count + 2);
                cursor += 2;
                word_p = (int16_t *)vfmt_get_next_arg(cursor - 1);
                if (*word_p > 0) {
                    len = *word_p;
                } else {
                    tmp_ptr = word_p;
                    word_p = (int16_t *)vfmt_get_next_arg(&tmp_ptr);
                    len = word_p[1];
                }
                str = (const char *)vfmt_get_next_arg(cursor - 2);
            } else {
                /* 0x00E6AD24 .. 0x00E6AD38: one argument, the text */
                arg_count++;
                cursor++;
                str = (const char *)vfmt_get_next_arg(cursor - 1);
            }

            /* 0x00E6AD3C .. 0x00E6AD44: a width caps the length */
            if (width >= 0 && len > width) {
                len = width;
            }

            /* 0x00E6AD46 .. 0x00E6AD84: Z / L / U modifiers */
            keep_blanks = false;
            uppercase = false;
            lowercase = false;
            d1 = (int16_t)(f.spec_len - 1);
            for (d0 = 1; d1 != -1; d1--, d0++) {
                if (f.spec[d0 - 1] == 'Z') {
                    keep_blanks = true;
                } else if (f.spec[d0 - 1] == 'L') {
                    lowercase = true;
                } else if (f.spec[d0 - 1] == 'U') {
                    uppercase = true;
                }
            }

            /* 0x00E6AD88 .. 0x00E6AD8E: the trailing-blank-dropping copy
             * is taken when Z is NOT given and no width was given: blanks
             * are counted and only emitted when a non-blank follows */
            if (keep_blanks >= 0 && width < 0) {
                /* 0x00E6ADF4 .. 0x00E6AE80 */
                d4 = 0;
                d0 = (int16_t)(len - 1);
                if (d0 < 0) {
                    continue;
                }
                for (d3 = 1; d0 != -1; d0--, d3++) {
                    if (str[d3 - 1] == ' ' || (uint8_t)str[d3 - 1] == 0xa0) {
                        d4++;
                        continue;
                    }
                    if (d4 > 0) {
                        d2 = (int16_t)(d4 - 1);
                        if (d2 >= 0) {
                            for (; d2 != -1; d2--) {
                                vfmt_output_char(&f, &vfmt_$main_blank_00e6afe0);
                            }
                        }
                    }
                    d4 = 0;
                    ch = str[d3 - 1];
                    if (uppercase < 0) {
                        if ((uint8_t)ch >= 'a' && (uint8_t)ch <= 'z') {
                            ch = (char)(ch - 0x20);
                        }
                    } else if (lowercase < 0) {
                        if ((uint8_t)ch >= 'A' && (uint8_t)ch <= 'Z') {
                            ch = (char)(ch + 0x20);
                        }
                    }
                    vfmt_output_char(&f, &ch);
                }
                continue;
            }

            /* 0x00E6AD90 .. 0x00E6ADE0: plain copy */
            d0 = (int16_t)(len - 1);
            if (d0 >= 0) {
                for (d3 = 1; d0 != -1; d0--, d3++) {
                    ch = str[d3 - 1];
                    if (uppercase < 0) {
                        if ((uint8_t)ch >= 'a' && (uint8_t)ch <= 'z') {
                            ch = (char)(ch - 0x20);
                        }
                    } else if (lowercase < 0) {
                        if ((uint8_t)ch >= 'A' && (uint8_t)ch <= 'Z') {
                            ch = (char)(ch + 0x20);
                        }
                    }
                    vfmt_output_char(&f, &ch);
                }
            }
            /* 0x00E6ADE4 .. 0x00E6ADF0: pad to the width */
            if (width <= len) {
                continue;
            }
            d0 = (int16_t)(width - len - 1);
            goto blanks;
        }

        if (c == '(') {
            /* 0x00E6AE8A .. 0x00E6AEA4 */
            vfmt_parse_number(&f, &repeat_count);
            if (repeat_count < 1) {
                repeat_count = 1;
            }
            repeat_count = (int16_t)(repeat_count - 1);
            repeat_pos = pos;
            continue;
        }

        if (c == ')') {
            /* 0x00E6AEB4 .. 0x00E6AEC0 */
            if (repeat_count > 0) {
                repeat_count--;
                pos = repeat_pos;
            }
            continue;
        }

        if (c == '%') {
            vfmt_output_char(&f, &vfmt_$main_percent_00e6afde);   /* 0x00E6AED0 */
            continue;
        }

        if (c == 'T') {
            /* 0x00E6AEE6 .. 0x00E6AF24: the column, from the modifiers or
             * from a word argument (with the same "word after it" rule) */
            vfmt_parse_number(&f, &width);
            d3 = width;
            if (d3 < 0) {
                arg_count++;
                cursor++;
                word_p = (int16_t *)vfmt_get_next_arg(cursor - 1);
                if (*word_p > 0) {
                    d3 = *word_p;
                } else {
                    tmp_ptr = word_p;
                    word_p = (int16_t *)vfmt_get_next_arg(&tmp_ptr);
                    d3 = word_p[1];
                }
            }
            /* 0x00E6AF28 .. 0x00E6AF38: out of range -> the next column */
            if (d3 <= 0 || d3 >= 0x400) {
                d3 = (int16_t)(*out_len + 1);
            }
            /* 0x00E6AF3C .. 0x00E6AF48 */
            if (*out_len > max_written) {
                max_written = *out_len;
            }
            /* 0x00E6AF4C .. 0x00E6AF70: blank columns max_written+1 ..
             * d3-1 (1-based), with no length check */
            if (d3 > max_written) {
                d2 = (int16_t)(max_written + 1);
                d0 = (int16_t)(d3 - 1 - d2);
                if (d0 >= 0) {
                    for (d1 = d0; d1 != -1; d1--, d2++) {
                        buf[d2 - 1] = ' ';
                    }
                }
            }
            /* 0x00E6AF74 .. 0x00E6AF7A */
            *out_len = (int16_t)(d3 - 1);
            continue;
        }

        if (c == 'X') {
            /* 0x00E6AF84 .. 0x00E6AF9E: at least one blank */
            vfmt_parse_number(&f, &x_count);
            d1 = x_count;
            if (d1 < 1) {
                d1 = 1;
            }
            d0 = (int16_t)(d1 - 1);
            goto blanks;
        }

        continue;

blanks:
        /* 0x00E6AFA0 .. 0x00E6AFAE: d0 + 1 blanks, none when negative */
        if (d0 < 0) {
            continue;
        }
        for (d2 = d0; d2 != -1; d2--) {
            vfmt_output_char(&f, &vfmt_$main_blank_00e6afe0);
        }
    }

too_long:
    /* 0x00E6AFBE .. 0x00E6AFCC: "??" and out without the max_written fix-up */
    vfmt_output_char(&f, &vfmt_$main_query_00e6afdc);
    vfmt_output_char(&f, &vfmt_$main_query_00e6afdc);
    (void)arg_count;
    (void)tmp_ptr;
}
