/*
 * AST_$GET_COMMON_ATTRIBUTES - Get common object attributes
 *
 * Retrieves a subset of commonly-used attributes for an object.
 * Calls AST_$GET_ATTRIBUTES internally and extracts specific fields.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   flags - Lookup flags
 *   attrs - Output buffer for common attributes
 *   status - Status return
 *
 * Original address: 0x00e04a00
 */

#include "ast.h"

void AST_$GET_COMMON_ATTRIBUTES(uid_t *uid, uint16_t flags, void *attrs, status_$t *status)
{
    uint32_t full_attrs[36];  /* Full attribute buffer (144 bytes) */
    uint8_t *out = (uint8_t *)attrs;
    int i;

    /* Get full attributes */
    AST_$GET_ATTRIBUTES(uid, flags, full_attrs, status);

    /* Copy selected fields to output */
    /* Output offset 0x00: First attribute word */
    ((uint32_t *)out)[0] = full_attrs[0];

    /* Output offset 0x04: Another attribute word */
    ((uint32_t *)out)[1] = full_attrs[5];  /* Offset 0x14 in full attrs */

    /* Output offset 0x08: 12 bytes of data */
    uint8_t *src = (uint8_t *)&full_attrs[15];  /* Offset 0x3C in full attrs */
    for (i = 0; i < 12; i++) {
        out[8 + i] = src[i];
    }

    /* Output offset 0x14: 2 bytes */
    ((uint16_t *)out)[10] = ((uint16_t *)full_attrs)[14];  /* Offset 0x1C */

    /* Process flag byte at offset 0x16 */
    uint8_t flag_byte = ((uint8_t *)full_attrs)[37];  /* Offset 0x25 */

    out[0x16] &= 0x7F;
    if ((int8_t)flag_byte < 0) {
        out[0x16] |= 0x80;
    }

    out[0x16] &= 0xBF;
    if ((flag_byte & 0x40) != 0) {
        out[0x16] |= 0x40;
    }

    out[2] &= 0xFD;
    if ((flag_byte & 0x20) != 0) {
        out[2] |= 0x02;
    }

    out[2] &= 0xFE;
    if ((flag_byte & 0x10) != 0) {
        out[2] |= 0x01;
    }
}
