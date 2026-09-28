/*
 * NAME_$VALIDATE - Classify the start of a pathname (0x00E49F4C, 158 bytes;
 * SAU2 map: NAMEU segment at E49E48)
 *
 * Decides how a pathname begins and reports it in *start_path_type:
 *   0  start_path_$error      length above 256
 *   1  start_path_$relative   anything not covered below (including empty)
 *   3  start_path_$absolute   leading '/'
 *   4  start_path_$network    leading "//"
 *   5  start_path_$node_data  exactly "`node_data", or "`node_data/" prefix
 * *consumed is INCREMENTED once per leading '/' recognised (it is not
 * initialised here - the caller's slot is whatever it was).  The function
 * result is always TRUE (`st D0b` at 0x00E49FDE).
 *
 * Frame (A6+): 0x08 path -> D2, 0x0C path_len -> A0, 0x10 consumed -> A1,
 * 0x14 start_path_type -> A2.
 */

#include "name/name_internal.h"

/*
 * Literal pool after the function (image bytes at 0xE49FEA:
 * `00 0a 00 0b 60 6e 6f 64 65 5f 64 61 74 61 60 6e 6f 64 65 5f 64 61 74 61 2f`).
 * `pea (d,PC)` addresses are PC = instruction + 2.
 */

/* 0xE49FEA, word 10: both NAMEQ lengths of the exact-match compare
 * (`pea (0x3c,PC)` at 0x00E49FAC and `pea (0x34,PC)` at 0x00E49FB4). */
static const uint16_t name_$validate_len10_00e49fea = 10;

/* 0xE49FEC, word 11: both NAMEQ lengths of the prefix compare
 * (`pea (0x2a,PC)` at 0x00E49FC0 and `pea (0x22,PC)` at 0x00E49FC8). */
static const uint16_t name_$validate_len11_00e49fec = 11;

/* 0xE49FEE, 10 bytes: "`node_data" (`pea (0x3c,PC)` at 0x00E49FB0). */
static const char name_$validate_node_data_00e49fee[10] = {
    '`', 'n', 'o', 'd', 'e', '_', 'd', 'a', 't', 'a'
};

/* 0xE49FF8, 11 bytes: "`node_data/" (`pea (0x32,PC)` at 0x00E49FC4). */
static const char name_$validate_node_data_slash_00e49ff8[11] = {
    '`', 'n', 'o', 'd', 'e', '_', 'd', 'a', 't', 'a', '/'
};

boolean NAME_$VALIDATE(char *path, uint16_t *path_len, int16_t *consumed,
                       start_path_type_t *start_path_type)
{
    uint16_t ch;

    /* 0x00E49F64-0x00E49F6C: `cmpi.w #0x100,(A0) / bls` - over 256 bytes */
    if (*path_len > NAME_$MAX_PNAME_LEN) {
        *start_path_type = start_path_$error;
        goto done;
    }

    *start_path_type = start_path_$relative;                /* 0x00E49F6E */

    if (*path_len == 0) {                                   /* 0x00E49F72 */
        goto done;
    }

    /* 0x00E49F76-0x00E49F88: first byte, zero-extended to a word */
    ch = (uint8_t)path[0];
    if (ch == 0x2F) {
        /* 0x00E49F8A-0x00E49FA4 */
        *start_path_type = start_path_$absolute;
        *consumed = (int16_t)(*consumed + 1);
        /* `cmpi.w #0x2,(A0) / bcs`: a one-byte path stops here */
        if (*path_len >= 2 && (uint8_t)path[1] == 0x2F) {
            *start_path_type = start_path_$network;
            *consumed = (int16_t)(*consumed + 1);
        }
        goto done;
    }
    if (ch != 0x60) {                                       /* 0x00E49F82 */
        goto done;
    }

    /* 0x00E49FA6-0x00E49FD8: '`' - exact "`node_data" when the length is
     * 10, the "`node_data/" prefix when it is longer, nothing when shorter. */
    if (*path_len == 10) {
        if (NAMEQ(path, (uint16_t *)&name_$validate_len10_00e49fea,
                  (char *)name_$validate_node_data_00e49fee,
                  (uint16_t *)&name_$validate_len10_00e49fea) < 0) {
            *start_path_type = start_path_$node_data;       /* 0x00E49FDA */
        }
    } else if (*path_len > 10) {                            /* `bls` exits */
        if (NAMEQ(path, (uint16_t *)&name_$validate_len11_00e49fec,
                  (char *)name_$validate_node_data_slash_00e49ff8,
                  (uint16_t *)&name_$validate_len11_00e49fec) < 0) {
            *start_path_type = start_path_$node_data;       /* 0x00E49FDA */
        }
    }

done:
    return (boolean)-1;                                     /* 0x00E49FDE st D0b */
}
