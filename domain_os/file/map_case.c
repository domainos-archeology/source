/*
 * MAP_CASE - Convert a Unix-style pathname to the Domain/OS case-mapped form
 * (0x00E53EF8, 476 bytes; SAU2 map: OLD_DIR segment, symbol MAP_CASE)
 *
 * Per input character (0x00E53F30-0x00E540C0, one `dbf` pass per byte):
 *   - first byte of a component that is '`' or '~'       -> ':' + char
 *   - first byte of a component that is '.':
 *       '.' at end or before '/'                          -> '.'
 *       '..' at end or before '/'                         -> '.'  (the second
 *                                                             '.' is handled
 *                                                             on its own pass)
 *       any other '.'                                     -> ':' + '.'
 *   - 'A'..'Z'                                            -> ':' + char,
 *                                                             WITHOUT a room check
 *   - 'a'..'z'                                            -> char - 0x20
 *   - 0x01..0x1F, 0x7F..0xFF                              -> ':' '#' hi lo
 *   - ':'                                                 -> ':' ':'
 *   - ' '                                                 -> ':' '_'
 *   - '\'                                                 -> ':' '|'
 *   - anything else (including NUL and '/')               -> char; '/' marks
 *                                                             the next byte as
 *                                                             a component start
 *
 * Frame (A6+):
 *   0x08 name         (long)  -> D5, input bytes, indexed 1-based via A4
 *   0x0C name_len     (long)  -> A3, pointer to the input length word
 *   0x10 output       (long)  -> A1
 *   0x14 max_out_len  (long)  -> A2, pointer to the output capacity word
 *   0x18 out_len      (long)  -> A0, the output count lives in *out_len
 *   0x1C truncated    (long)  -> A4 at entry/exit, byte flag
 *
 * Registers: D2w = 1-based input index, D3w = dbf counter (len-1), D4w =
 * 1-based index of the first byte of the current component.  The output
 * count is kept in memory at (A0) and *max_out_len / *name_len are re-read
 * from memory at every use, which the C reproduces.
 *
 * The truncated flag is set to 0xFF at 0x00E53F22 and only cleared by the
 * normal exit at 0x00E540C4; every "no room" exit branches to 0x00E540CA and
 * leaves it set.
 */

#include "file/file_internal.h"

void MAP_CASE(char *name, int16_t *name_len, char *output,
              int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    int16_t  i;                 /* D2w: 1-based input index */
    int16_t  count;             /* D3w: dbf counter */
    int16_t  comp_start;        /* D4w: 1-based index of the component's first byte */
    uint8_t  ch;                /* D0b */
    uint8_t  lo;
    int16_t  n;

    *out_len = 0;                                   /* 0x00E53F10 clr.w (A0) */

    /* 0x00E53F16 tst.w (A3) / ble.w 0x00E540C4 */
    if (*name_len <= 0) {
        goto done;
    }

    comp_start = 1;                                 /* 0x00E53F20 moveq #1,D4 */
    *truncated = 0xFF;                              /* 0x00E53F22 st (A4) */

    /* 0x00E53F24-0x00E53F28: D0 = len - 1; a negative count exits (cannot
     * happen after the test above, reproduced anyway). */
    count = (int16_t)(*name_len - comp_start);
    if (count < 0) {
        goto done;
    }
    i = comp_start;                                 /* 0x00E53F2E */

    /* 0x00E53F30-0x00E540C0: dbf loop, count+1 passes. */
    for (;;) {
        /* 0x00E53F30-0x00E53F34: no room at all -> truncated exit. */
        if (*out_len >= *max_out_len) {
            goto truncated_exit;
        }

        ch = (uint8_t)name[i - 1];                  /* 0x00E53F3A */

        /* 0x00E53F3E-0x00E53F56: component-start specials. */
        if (i == comp_start && (ch == 0x60 || ch == 0x2E || ch == 0x7E)) {
            if (ch != 0x2E) {                       /* 0x00E53F58 */
                goto escape_checked;
            }
            /* 0x00E53F60-0x00E53F6A: '.' at end, or before '/'. */
            if (i == *name_len || (uint8_t)name[i] == 0x2F) {
                *out_len = (int16_t)(*out_len + 1);         /* 0x00E53F6C */
                output[*out_len - 1] = 0x2E;                /* 0x00E53F70 */
                goto next;
            }
            /* 0x00E53F7A: '.' followed by something other than '.'. */
            if ((uint8_t)name[i] != 0x2E) {
                goto escape_checked;
            }
            /* 0x00E53F84-0x00E53F98: '..' at end, or before '/'. */
            if ((int32_t)i + 1 != (int32_t)*name_len &&
                (uint8_t)name[i + 1] != 0x2F) {
                goto escape_checked;
            }
            *out_len = (int16_t)(*out_len + 1);             /* 0x00E53F9C */
            n = *out_len;
            output[n - 1] = ch;                             /* 0x00E54066 */
            goto next;
        }

        /* 0x00E53FA6-0x00E53FB0: 'A'..'Z' -> ':' + ch with NO room check
         * (branches straight to 0x00E5405C). */
        if (ch >= 0x41 && ch <= 0x5A) {
            goto escape_unchecked;
        }

        /* 0x00E53FB4-0x00E53FC6: 'a'..'z' -> upper. */
        if (ch >= 0x61 && ch <= 0x7A) {
            *out_len = (int16_t)(*out_len + 1);
            ch = (uint8_t)(ch - 0x20);
            n = *out_len;
            output[n - 1] = ch;                             /* 0x00E53FA0 */
            goto next;
        }

        /* 0x00E53FC8-0x00E53FE2: `scc`/`sls`/`and.b`/`bmi` = 1 <= ch <= 0x1F;
         * otherwise ch < 0x7F skips, and the `cmpi.b #-0x1 / bhi` can never
         * branch, so 0x7F..0xFF are hex-escaped too. */
        if ((ch >= 0x01 && ch <= 0x1F) || ch >= 0x7F) {
            /* 0x00E53FE4-0x00E53FF0: room for four bytes? (longword compare) */
            if ((int32_t)*out_len + 4 > (int32_t)*max_out_len) {
                goto truncated_exit;
            }
            *out_len = (int16_t)(*out_len + 4);             /* 0x00E53FF4 */
            n = *out_len;
            output[n - 4] = 0x3A;                           /* 0x00E53FF8 ':' */
            output[n - 3] = 0x23;                           /* 0x00E53FFE '#' */
            /* 0x00E54004-0x00E54016: high nibble always gets +0x30, so
             * nibbles 10..15 come out as ':'..'?' - original quirk kept. */
            output[n - 2] = (char)(uint8_t)((ch >> 4) + 0x30);
            /* 0x00E5401A-0x00E5402E: low nibble, > 9 gets +0x57 ('a'..'f'). */
            lo = (uint8_t)(ch & 0x0F);
            if (lo > 9) {
                output[n - 1] = (char)(uint8_t)(lo + 0x57);
            } else {
                output[n - 1] = (char)(uint8_t)(lo + 0x30);
            }
            goto next;
        }

        /* 0x00E54036-0x00E5404C: dispatch on the byte. */
        if (ch == 0x3A) {
            goto escape_checked;                            /* ':' -> "::" */
        }
        if (ch == 0x20) {
            /* 0x00E5406C-0x00E5408A: ' ' -> ":_" */
            if ((int32_t)*out_len + 2 > (int32_t)*max_out_len) {
                goto truncated_exit;
            }
            *out_len = (int16_t)(*out_len + 2);
            n = *out_len;
            output[n - 2] = 0x3A;
            output[n - 1] = 0x5F;
            goto next;
        }
        if (ch == 0x5C) {
            /* 0x00E5408C-0x00E540AA: '\' -> ":|" */
            if ((int32_t)*out_len + 2 > (int32_t)*max_out_len) {
                goto truncated_exit;
            }
            *out_len = (int16_t)(*out_len + 2);
            n = *out_len;
            output[n - 2] = 0x3A;
            output[n - 1] = 0x7C;
            goto next;
        }

        /* 0x00E540AC-0x00E540BC: pass through; '/' starts a new component. */
        *out_len = (int16_t)(*out_len + 1);
        n = *out_len;
        output[n - 1] = (char)ch;
        if (ch == 0x2F) {
            comp_start = (int16_t)(i + 1);
        }
        goto next;

escape_checked:
        /* 0x00E5404E-0x00E5405A: room for two bytes? */
        if ((int32_t)*out_len + 2 > (int32_t)*max_out_len) {
            goto truncated_exit;
        }
escape_unchecked:
        /* 0x00E5405C-0x00E54066: ':' + ch */
        *out_len = (int16_t)(*out_len + 2);
        n = *out_len;
        output[n - 2] = 0x3A;
        output[n - 1] = (char)ch;

next:
        /* 0x00E540BE-0x00E540C0: addq.w #1,D2w / dbf D3w */
        i = (int16_t)(i + 1);
        count = (int16_t)(count - 1);
        if (count == -1) {
            break;
        }
    }

done:
    *truncated = 0;                                 /* 0x00E540C4-0x00E540C8 */
truncated_exit:
    return;                                         /* 0x00E540CA */
}
