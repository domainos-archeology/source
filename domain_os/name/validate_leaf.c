/*
 * name_$validate_leaf - Case-map and validate one directory leaf name
 * (0x00E54414, 154 bytes; SAU2 map: OLD_DIR segment at E53EF8, no symbol -
 * module-local, tree name kept)
 *
 * Runs MAP_CASE over the name into the caller's buffer and then checks the
 * mapped result against two Pascal character sets kept in the OLD_DIR module
 * data area (A5 = 0xE7FD24, inherited from the DIR_$OLD_* caller):
 *   A5+0x00  NAME_$OLD_DIR_DATA.leaf_char_set        bytes 2..len
 *   A5+0x20  NAME_$OLD_DIR_DATA.leaf_first_char_set  the first byte only
 * Returns 0xFF when the name is acceptable, else 0.
 *
 * Frame (A6+): 0x08 name (long), 0x0C name_len (word -> D3w; its ADDRESS is
 * what MAP_CASE gets, `pea (0xc,A6)` at 0x00E5443C), 0x0E parsed -> A2,
 * 0x12 parsed_len -> A3.  Local: -0x0A MAP_CASE's truncated flag.
 *
 * Rejections, in order (each `b.. 0x00E544A2` leaves D2b = 0):
 *   0x00E5442A  name_len > 32
 *   0x00E5444C  MAP_CASE reported truncation (flag byte negative)
 *   0x00E54452  mapped length 0
 *   0x00E54456  mapped length > 32
 *   0x00E5445C  first mapped byte is '\'
 *   0x00E54472  first byte not in the first-char set
 *   0x00E54494  any byte 2..len not in the char set
 */

#include "name/name_internal.h"

/* 0xE544AE, word 0x0020: MAP_CASE's maximum output length, the word right
 * after this routine's `rts` (`pea (0x76,PC)` at 0x00E54436).  Shared with
 * the DIR routines that reach the same cell (name/name.h). */
const int16_t name_$leaf_max_len_00e544ae = 0x0020;

/*
 * Pascal set membership as compiled at 0x00E54462-0x00E54476: with the set
 * bound fixed at 0xFF there is no range test; the byte is
 * (0xFF - ch) >> 3 (so 0..31) and the bit is ch & 7 (`btst.b Dn,(mem)`
 * numbers bits modulo 8).
 */
static int name_$leaf_in_set(const uint8_t *set, uint8_t ch)
{
    return (set[(uint16_t)(0xFF - ch) >> 3] >> (ch & 7)) & 1;
}

/*
 * Both sets are 32-byte `set of char' values (name/name.h), so the bound-0xFF
 * index stays inside each.  Bytes 28..31 of the first-character set (ch <
 * 0x20) are also the storage of NAME_$OLD_DIR_DATA.lock_slot[0], the bias slot
 * of the lock-slot table; the image has them zero and only a process 0 lock
 * would write them.  The union in name/name.h reproduces that overlay.
 */

int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    int8_t   result;                        /* D2b */
    int8_t   truncated;                     /* A6-0x0A */
    int16_t  len_slot = (int16_t)name_len;  /* the (0xc,A6) argument slot */
    uint16_t len;                           /* D1w */
    uint8_t  ch;
    int16_t  count;
    int16_t  i;

    result = 0;                                             /* 0x00E54428 */

    if (name_len > 0x20) {                                  /* 0x00E5442A bhi */
        goto done;
    }

    /* 0x00E54430-0x00E54448: MAP_CASE(name, &name_len, parsed, &0x20,
     * parsed_len, &truncated) */
    MAP_CASE(name, &len_slot, (char *)parsed,
             (int16_t *)&name_$leaf_max_len_00e544ae,
             (int16_t *)parsed_len, (uint8_t *)&truncated);

    if (truncated < 0) {                                    /* 0x00E5444C bmi */
        goto done;
    }

    len = *parsed_len;                                      /* 0x00E54452 */
    if (len == 0) {
        goto done;
    }
    if (len > 0x20) {                                       /* 0x00E54456 bhi */
        goto done;
    }
    if (parsed[0] == 0x5C) {                                /* 0x00E5445C */
        goto done;
    }

    /* 0x00E54462-0x00E54476: first byte against A5+0x20 */
    ch = parsed[0];
    if (!name_$leaf_in_set(NAME_$OLD_DIR_DATA.leaf_first_char_set, ch)) {
        goto done;
    }

    /* 0x00E54478-0x00E5447C: D1 = len - 2; negative (len == 1) -> accept */
    count = (int16_t)(len - 2);
    if (count < 0) {
        goto accept;
    }

    /* 0x00E54480-0x00E5449C: bytes 2..len (1-based) against A5+0x00,
     * dbf count = len-2 so len-1 passes */
    i = 2;
    do {
        ch = parsed[i - 1];
        if (!name_$leaf_in_set(NAME_$OLD_DIR_DATA.leaf_char_set, ch)) {
            goto done;                                      /* 0x00E54498 */
        }
        i = (int16_t)(i + 1);
        count = (int16_t)(count - 1);
    } while (count != -1);

accept:
    result = (int8_t)-1;                                    /* 0x00E544A0 st D2b */

done:
    return result;                                          /* 0x00E544A2 */
}
