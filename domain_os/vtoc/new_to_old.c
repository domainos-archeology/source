/*
 * VTOCE_$NEW_TO_OLD - Convert new format VTOCE to old format
 *
 * Original address: 0x00e384c4
 * Size: 192 bytes
 *
 * Converts a new format VTOCE (0x150 bytes) to old format (0xCC bytes).
 * Some fields are lost in the conversion.
 *
 * Frame (link.w A6,0x0; A2/A3 saved):
 *   (0x8,A6)   new_vtoce   pointer -> A0 (source)
 *   (0xc,A6)   flags       pointer to a byte; negative selects the parent
 *                          UID at new+0x04 and sets bit 3 of old byte 0x18
 *                          (0x00E38502 .. 0x00E38518), otherwise the one
 *                          at new+0x88 and the low nibble is cleared
 *   (0x10,A6)  old_vtoce   pointer -> A1 (destination)
 *
 * Both records are treated as byte arrays; every longword / word move is
 * between same-sized fields at fixed offsets, so the copies are byte-order
 * neutral.  Re-checked against the disassembly 2026-09-19 (0x00E384C4 ..
 * 0x00E38583): the body was already faithful; the two byte sign tests are
 * now on int8_t rather than `char`.
 */

#include "vtoc/vtoc_internal.h"

void VTOCE_$NEW_TO_OLD(void *new_vtoce_ptr, char *flags, void *old_vtoce_ptr)
{
    uint32_t *new_vtoce = (uint32_t *)new_vtoce_ptr;
    uint32_t *old_vtoce = (uint32_t *)old_vtoce_ptr;
    int16_t i;
    uint8_t *src;
    uint8_t *dst;
    int8_t ext_flags;

    /* Copy first long (type_mode, flags, etc.) */
    old_vtoce[0] = new_vtoce[0];

    /* 0x00E384D6 .. 0x00E384E6: ext_flags bit 7 -> old status bit 1
     * (`smi` / `lsr.b #7` / `add.b D0b,D0b`) */
    ext_flags = (int8_t)((uint8_t *)new_vtoce)[0x65];
    ((uint8_t *)old_vtoce)[2] &= 0xFD;
    if (ext_flags < 0) {
        ((uint8_t *)old_vtoce)[2] |= 0x02;
    }

    /* Clear byte 3 */
    ((uint8_t *)old_vtoce)[3] = 0;

    /* Copy name (16 bytes from offset 4 to offset 4) */
    src = (uint8_t *)(new_vtoce + 1);
    dst = (uint8_t *)(old_vtoce + 1);
    for (i = 0xF; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E384FE tst.b (A2) / bpl.b: copy the parent UID based on flags */
    if ((int8_t)*flags < 0) {
        /* Use alternate parent from new offset 0x04 */
        old_vtoce[5] = new_vtoce[1];
        old_vtoce[6] = new_vtoce[2];
        /* Set bit 3 of old offset 0x18 */
        ((uint8_t *)old_vtoce)[0x18] |= 0x08;
    } else {
        /* Use normal parent from new offset 0x88 */
        old_vtoce[5] = new_vtoce[0x22];
        old_vtoce[6] = new_vtoce[0x23];
        /* Clear low nibble of old offset 0x18 */
        ((uint8_t *)old_vtoce)[0x18] &= 0xF0;
    }

    /* Copy DTM from new offset 0x14 to old offset 0x1C */
    old_vtoce[7] = new_vtoce[5];
    old_vtoce[8] = new_vtoce[6];

    /* Copy ACL UID from new offset 0x24 to old offset 0x24 */
    old_vtoce[9] = new_vtoce[9];

    /* Copy EOF block from new offset 0x1C to old offset 0x28 */
    old_vtoce[10] = new_vtoce[7];

    /* Copy unused word from new offset 0x20 to old offset 0x34 */
    *(uint16_t *)((uint8_t *)old_vtoce + 0x34) = *(uint16_t *)(new_vtoce + 8);

    /* Copy current_length from new offset 0x3C to old offset 0x2C */
    old_vtoce[11] = new_vtoce[0xF];
    old_vtoce[12] = new_vtoce[0x10];

    /* Convert link count from new offset 0x74 to old offset 0x36 */
    if (*(uint16_t *)(new_vtoce + 0x1D) >= 0xFFF5) {
        *(int16_t *)((uint8_t *)old_vtoce + 0x36) = -2; /* 0xFFFE */
    } else {
        *(int16_t *)((uint8_t *)old_vtoce + 0x36) =
            *(int16_t *)(new_vtoce + 0x1D) - 1;
    }

    /* Copy DTU from new offset 0x44 to old offset 0x38 */
    old_vtoce[14] = new_vtoce[0x11];

    /* Clear old offset 0x3C */
    old_vtoce[15] = 0;
}
