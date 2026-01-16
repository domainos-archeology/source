/*
 * VTOCE_$NEW_TO_OLD - Convert new format VTOCE to old format
 *
 * Original address: 0x00e384c4
 * Size: 192 bytes
 *
 * Converts a new format VTOCE (0x150 bytes) to old format (0xCC bytes).
 * Some fields are lost in the conversion.
 */

#include "vtoc/vtoc_internal.h"

void VTOCE_$NEW_TO_OLD(void *new_vtoce_ptr, char *flags, void *old_vtoce_ptr)
{
    uint32_t *new_vtoce = (uint32_t *)new_vtoce_ptr;
    uint32_t *old_vtoce = (uint32_t *)old_vtoce_ptr;
    int16_t i;
    uint8_t *src;
    uint8_t *dst;
    char ext_flags;

    /* Copy first long (type_mode, flags, etc.) */
    old_vtoce[0] = new_vtoce[0];

    /* Extract ext_flags bit 7 -> old status bit 1 */
    ext_flags = ((char *)new_vtoce)[0x65];
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

    /* Copy parent UID based on flags */
    if (*flags < 0) {
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
