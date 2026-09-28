/*
 * UNMAP_CASE - Convert a Domain/OS case-mapped pathname back to Unix form
 * (0x00E540D4, 734 bytes; SAU2 map: OLD_DIR segment, symbol UNMAP_CASE)
 *
 * Reverses MAP_CASE.  Per input byte (0x00E5410C-0x00E543A2):
 *   - '\'                       -> "../", preceded by '/' unless the output is
 *                                  empty or already ends in '/'
 *   - 'A'..'Z'                  -> lower case
 *   - ':' as the LAST byte      -> ':' and stop scanning
 *   - ':' + c, c in the 0xE54404 set ['A'..'Z','`','~',':','.'] -> c
 *   - ':' + 'a'..'z'            -> upper case
 *   - ':' + '0'..'9'            -> "!#%&+-?=@^"[digit]
 *   - ':' + '_' / '|' / '$'     -> ' ' / '\' / '$'
 *   - ':' + '#' [h1 [h2]]       -> the byte h1h2; a non-hex h1 counts as 0 and
 *                                  h2 is STILL consumed; a non-hex h2 leaves
 *                                  h1 << 4
 *   - ':' + anything else       -> that byte
 *   - anything else             -> unchanged
 * Afterwards the 1-based count is turned into a length, a NUL is stored when
 * there is room, and the truncated flag is cleared.
 *
 * Frame (A6+):
 *   0x08 name         (long)  -> D4, bytes indexed 1-based via A4
 *   0x0C name_len     (long)  -> A2, pointer to the input length word
 *   0x10 output       (long)  -> A1
 *   0x14 max_out_len  (long)  -> A3 when needed
 *   0x18 out_len      (long)  -> A0; the 1-based output count lives in *out_len
 *   0x1C truncated    (long)  -> A3 at entry/exit
 *
 * Character classes are Pascal sets, stored as bit tables in the code
 * segment (0x00E543C6-0x00E54413) and tested with the compiler's
 * `moveq #bound,Dn / sub.w ch,Dn / bcs / lsr.w #3 / btst.b ch,(table,Dn)`
 * idiom, which unmap_$in_set reproduces byte for byte.
 */

#include "file/file_internal.h"

/*
 * unmap_$in_set - Pascal set membership as compiled at e.g. 0x00E54164:
 * ch > bound is "not a member" (the `bcs`); otherwise byte (bound-ch)>>3,
 * bit ch&7 (`btst.b Dn,(mem)` numbers bits modulo 8).
 */
static int unmap_$in_set(const uint8_t *set, uint16_t bound, uint8_t ch)
{
    uint16_t d;

    if (ch > bound) {
        return 0;
    }
    d = (uint16_t)(bound - ch);
    return (set[d >> 3] >> (ch & 7)) & 1;
}

/* 0x00E543C6, 14 bytes, bound 0x6F: ['a'..'f'] (0x00E54300, 0x00E54360). */
static const uint8_t unmap_$set_hex_lower[14] = {
    0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* 0x00E543D4, 10 bytes, bound 0x4F: ['A'..'F'] (0x00E5431A, 0x00E5437A). */
static const uint8_t unmap_$set_hex_upper[10] = {
    0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

/* 0x00E543E0, 12 bytes, bound 0x5F: ['A'..'Z'] (0x00E54170). */
static const uint8_t unmap_$set_upper[12] = {
    0x07, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

/* 0x00E543EC, 8 bytes, bound 0x3F: ['0'..'9'] (0x00E541DA, 0x00E542E6,
 * 0x00E54346). */
static const uint8_t unmap_$set_digit[8] = {
    0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* 0x00E543F4, 16 bytes, bound 0x7F: ['a'..'z'] (0x00E541C0). */
static const uint8_t unmap_$set_lower[16] = {
    0x07, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* 0x00E54404, 16 bytes, bound 0x7F: ['A'..'Z', '`', '~', ':', '.'] - the
 * bytes MAP_CASE escapes with ':' and passes through unchanged (0x00E541AC). */
static const uint8_t unmap_$set_escaped_literal[16] = {
    0x40, 0x00, 0x00, 0x01, 0x07, 0xff, 0xff, 0xfe,
    0x04, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* 0x00E541FE jump table targets, ':0'..':9' (0x00E54212-0x00E54286). */
static const char unmap_$digit_escape[10] = {
    0x21, 0x23, 0x25, 0x26, 0x2B, 0x2D, 0x3F, 0x3D, 0x40, 0x5E
};

void UNMAP_CASE(char *name, int16_t *name_len, char *output,
                int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    int16_t  i;         /* D1w: 1-based input index */
    uint8_t  ch;        /* D0b */
    uint8_t  c2;        /* D2b/D3b: the byte after ':' / a hex digit */
    uint16_t val;       /* D0w on the hex path */
    int16_t  n;

    /* 0x00E540EC-0x00E540F8: only an EXACTLY zero length is the empty case;
     * a negative length skips the loop and goes through the epilogue. */
    if (*name_len == 0) {
        *truncated = 0;
        *out_len = 0;
        return;                                     /* 0x00E543BC */
    }

    *truncated = 0xFF;                              /* 0x00E54100 */
    *out_len = 1;                                   /* 0x00E54102 */
    i = 1;                                          /* 0x00E54106 */

    /* 0x00E543A0: `cmp.w (A2),D1w / ble` - loop while i <= *name_len. */
    while (i <= *name_len) {
        /* 0x00E5410C-0x00E54114: out of room -> exit, flag left at 0xFF. */
        if (*out_len > *max_out_len) {
            return;                                 /* 0x00E543BC */
        }

        ch = (uint8_t)name[i - 1];                  /* 0x00E5411A */

        if (ch == 0x5C) {                           /* 0x00E5411E */
            /* 0x00E54124-0x00E54138: separator unless empty or already '/'. */
            if (*out_len > 1) {
                n = *out_len;
                if ((uint8_t)output[n - 2] != 0x2F) {
                    output[n - 1] = 0x2F;
                    *out_len = (int16_t)(*out_len + 1);
                }
            }
            /* 0x00E5413A-0x00E54146: room for two more? (longword compare) */
            if ((int32_t)*out_len + 2 > (int32_t)*max_out_len) {
                return;                             /* 0x00E543BC */
            }
            n = *out_len;                           /* 0x00E5414A */
            output[n - 1] = 0x2E;
            output[n]     = 0x2E;
            output[n + 1] = 0x2F;
            *out_len = (int16_t)(*out_len + 2);     /* 0x00E5415E */
            goto advance;
        }

        /* 0x00E54164-0x00E5417E: bare upper case -> lower. */
        if (unmap_$in_set(unmap_$set_upper, 0x5F, ch)) {
            ch = (uint8_t)(ch + 0x20);
            goto emit_ch;
        }

        if (ch != 0x3A) {                           /* 0x00E54182 */
            goto emit_ch;
        }

        /* 0x00E5418A-0x00E54196: ':' as the last byte -> ':' and stop. */
        if (i == *name_len) {
            n = *out_len;
            output[n - 1] = (char)ch;
            *out_len = (int16_t)(*out_len + 1);
            goto epilogue;                          /* bra.w 0x00E543A6 */
        }

        i = (int16_t)(i + 1);                       /* 0x00E5419A */
        ch = (uint8_t)name[i - 1];                  /* 0x00E5419E */
        c2 = ch;

        /* 0x00E541A2-0x00E541B4: escaped literal -> as is. */
        if (unmap_$in_set(unmap_$set_escaped_literal, 0x7F, c2)) {
            goto emit_ch;
        }
        /* 0x00E541B8-0x00E541CE: ':' + lower -> upper. */
        if (unmap_$in_set(unmap_$set_lower, 0x7F, c2)) {
            ch = (uint8_t)(ch - 0x20);
            goto emit_ch;
        }
        /* 0x00E541D2-0x00E541FA: ':' + digit -> table. */
        if (unmap_$in_set(unmap_$set_digit, 0x3F, c2)) {
            val = (uint16_t)(c2 - 0x30);
            if (val >= 10) {                        /* 0x00E541EC bcc: never */
                goto advance;
            }
            n = *out_len;
            output[n - 1] = unmap_$digit_escape[val];
            goto advance;
        }

        /* 0x00E5428A-0x00E542A4 */
        if (c2 == 0x5F) {
            n = *out_len;
            output[n - 1] = 0x20;                   /* 0x00E542AA */
            goto advance;
        }
        if (c2 == 0x7C) {
            n = *out_len;
            output[n - 1] = 0x5C;                   /* 0x00E542B6 */
            goto advance;
        }
        if (c2 == 0x24) {
            n = *out_len;
            output[n - 1] = 0x24;                   /* 0x00E542C2 */
            goto advance;
        }
        if (c2 != 0x23) {
            val = ch;                               /* D0b still holds c2 */
            goto emit_val;                          /* 0x00E5438E */
        }

        /* ':#' hex escape, 0x00E542CC-0x00E5438A. */
        val = 0;                                    /* 0x00E542CC clr.w D0w */
        if (i >= *name_len) {                       /* 0x00E542CE bge */
            goto advance;                           /* nothing stored */
        }
        i = (int16_t)(i + 1);                       /* 0x00E542D4 */
        c2 = (uint8_t)name[i - 1];                  /* 0x00E542D8 */
        if (unmap_$in_set(unmap_$set_digit, 0x3F, c2)) {
            val = (uint16_t)(c2 - 0x30);            /* 0x00E542F0 */
        } else if (unmap_$in_set(unmap_$set_hex_lower, 0x6F, c2)) {
            val = (uint16_t)(c2 - 0x61 + 0x0A);     /* 0x00E5430A, 0x00E5432A */
        } else if (unmap_$in_set(unmap_$set_hex_upper, 0x4F, c2)) {
            val = (uint16_t)(c2 - 0x41 + 0x0A);     /* 0x00E54324, 0x00E5432A */
        }
        /* a non-hex first digit leaves val = 0 and falls into the second
         * digit anyway (0x00E54322 ble -> 0x00E5432E) */

        if (i >= *name_len) {                       /* 0x00E5432E bge */
            goto emit_val;
        }
        i = (int16_t)(i + 1);                       /* 0x00E54332 */
        val = (uint16_t)(val << 4);                 /* 0x00E54334 */
        c2 = (uint8_t)name[i - 1];                  /* 0x00E54336 */
        if (unmap_$in_set(unmap_$set_digit, 0x3F, c2)) {
            val = (uint16_t)(val + c2 - 0x30);      /* 0x00E54350 */
        } else if (unmap_$in_set(unmap_$set_hex_lower, 0x6F, c2)) {
            val = (uint16_t)(val + c2 - 0x61 + 0x0A);   /* 0x00E5436A, 0x00E5438A */
        } else if (unmap_$in_set(unmap_$set_hex_upper, 0x4F, c2)) {
            val = (uint16_t)(val + c2 - 0x41 + 0x0A);   /* 0x00E54384, 0x00E5438A */
        }
        /* a non-hex second digit leaves the shifted first one (0x00E54382) */

emit_val:
        /* 0x00E5438E-0x00E54390: store the low byte of D0. */
        n = *out_len;
        output[n - 1] = (char)(uint8_t)val;
        goto advance;

emit_ch:
        /* 0x00E54396-0x00E54398 */
        n = *out_len;
        output[n - 1] = (char)ch;

advance:
        /* 0x00E5439C-0x00E5439E */
        i = (int16_t)(i + 1);
        *out_len = (int16_t)(*out_len + 1);
    }

epilogue:
    /* 0x00E543A6-0x00E543BA: count -> length, NUL if room, flag cleared. */
    *out_len = (int16_t)(*out_len - 1);
    n = *out_len;
    if (n < *max_out_len) {
        output[n] = 0;
    }
    *truncated = 0;
}
