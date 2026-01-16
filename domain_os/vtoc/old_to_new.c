/*
 * VTOCE_$OLD_TO_NEW - Convert old format VTOCE to new format
 *
 * Original address: 0x00e19db8
 * Size: 422 bytes
 *
 * Converts an old format VTOCE (0xCC bytes) to new format (0x150 bytes).
 * Sets default values for ACL fields not present in old format.
 */

#include "vtoc/vtoc_internal.h"

/*
 * Default ACL data for new format conversion
 * Located at 0x00e19f60 in original binary
 */
static const uint32_t default_acl_data[3] = { 0, 0, 0 };

void VTOCE_$OLD_TO_NEW(void *old_vtoce_ptr, void *new_vtoce_ptr)
{
    uint8_t *old_vtoce = (uint8_t *)old_vtoce_ptr;
    uint32_t *new_vtoce = (uint32_t *)new_vtoce_ptr;
    int16_t i;
    uint8_t *src;
    uint8_t *dst;
    int16_t old_flags;
    uint16_t old_status;
    int16_t link_count;

    /* Clear destination */
    new_vtoce[0] = 0;

    /* Copy type_mode and flags (bytes 0-1) */
    ((uint8_t *)new_vtoce)[0] = old_vtoce[0];
    ((uint8_t *)new_vtoce)[1] = old_vtoce[1];

    /* Convert flags from old format word at offset 2 */
    old_flags = *(int16_t *)(old_vtoce + 2);

    /* Set bit 7 of new_flags based on sign of old_flags */
    ((uint8_t *)new_vtoce)[2] &= 0x7F;
    if (old_flags < 0) {
        ((uint8_t *)new_vtoce)[2] |= 0x80;
    }

    /* Copy bits 5-6 from old byte 2 */
    ((uint8_t *)new_vtoce)[2] &= 0x9F;
    ((uint8_t *)new_vtoce)[2] |= (old_vtoce[2] & 0x60);

    /* Get old status word for flag extraction */
    old_status = *(uint16_t *)(old_vtoce + 2);

    /* Extract bit 12 -> new bit 4 */
    ((uint8_t *)new_vtoce)[2] &= 0xEF;
    if (old_status & 0x1000) {
        ((uint8_t *)new_vtoce)[2] |= 0x10;
    }

    /* Extract bit 11 -> new bit 3 */
    ((uint8_t *)new_vtoce)[2] &= 0xF7;
    if (old_status & 0x0800) {
        ((uint8_t *)new_vtoce)[2] |= 0x08;
    }

    /* Extract bit 10 -> new bit 2 */
    ((uint8_t *)new_vtoce)[2] &= 0xFB;
    if (old_status & 0x0400) {
        ((uint8_t *)new_vtoce)[2] |= 0x04;
    }

    /* Extract bit 8 -> ext_flags bits 4-5 (at offset 0x65) */
    ((uint8_t *)new_vtoce)[0x65] &= 0xDF;
    if (old_status & 0x0100) {
        ((uint8_t *)new_vtoce)[0x65] |= 0x20;
    }
    ((uint8_t *)new_vtoce)[0x65] &= 0xEF;
    if (old_status & 0x0100) {
        ((uint8_t *)new_vtoce)[0x65] |= 0x10;
    }

    /* Copy name (16 bytes from offset 4 to offset 4) */
    src = old_vtoce + 4;
    dst = (uint8_t *)(new_vtoce + 1);
    for (i = 0xF; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* Clear middle section (0x1E longs starting at offset 0x14) */
    for (i = 0x1E; i >= 0; i--) {
        new_vtoce[5 + i] = 0;
    }

    /* Copy DTM (date/time modified) from old offset 0x1C to new offset 0x14 */
    new_vtoce[5] = *(uint32_t *)(old_vtoce + 0x1C);
    new_vtoce[6] = *(uint32_t *)(old_vtoce + 0x20);

    /* Copy EOF block from old offset 0x28 to new offset 0x1C */
    new_vtoce[7] = *(uint32_t *)(old_vtoce + 0x28);

    /* Copy unused field from old offset 0x34 to new offset 0x20 */
    *(uint16_t *)(new_vtoce + 8) = *(uint16_t *)(old_vtoce + 0x34);

    /* Copy ACL UID from old offset 0x24 to new offset 0x24 */
    new_vtoce[9] = *(uint32_t *)(old_vtoce + 0x24);

    /* Copy DTM to DTC (date/time created) at offset 0x34 */
    new_vtoce[0xD] = new_vtoce[7];
    *(uint16_t *)(new_vtoce + 0xE) = *(uint16_t *)(new_vtoce + 8);

    /* Copy DTM to DTA (date/time accessed) at offset 0x2C */
    new_vtoce[0xB] = new_vtoce[7];
    *(uint16_t *)(new_vtoce + 0xC) = *(uint16_t *)(new_vtoce + 8);

    /* Copy current_length from old offset 0x2C to new offset 0x3C */
    new_vtoce[0xF] = *(uint32_t *)(old_vtoce + 0x2C);

    /* Copy blocks_used from old offset 0x30 to new offset 0x40 */
    new_vtoce[0x10] = *(uint32_t *)(old_vtoce + 0x30);

    /* Copy DTU (date/time used) from old offset 0x38 to new offset 0x44 */
    new_vtoce[0x11] = *(uint32_t *)(old_vtoce + 0x38);

    /* Convert link count from old offset 0x36 */
    link_count = *(int16_t *)(old_vtoce + 0x36);
    link_count++;
    *(uint16_t *)(new_vtoce + 0x1D) = link_count;
    /* Clamp to max of 0xFFF5 */
    if ((uint16_t)link_count > 0xFFF4) {
        *(uint16_t *)(new_vtoce + 0x1D) = 0xFFF5;
    }

    /* Set default owner UID (PPO_$NIL_USER_UID) at offset 0x48 */
    new_vtoce[0x12] = PPO_$NIL_USER_UID.high;
    new_vtoce[0x13] = PPO_$NIL_USER_UID.low;

    /* Set default group UID (RGYC_$G_NIL_UID) at offset 0x50 */
    new_vtoce[0x14] = RGYC_$G_NIL_UID.high;
    new_vtoce[0x15] = RGYC_$G_NIL_UID.low;

    /* Set default org UID (PPO_$NIL_ORG_UID) at offset 0x58 */
    new_vtoce[0x16] = PPO_$NIL_ORG_UID.high;
    new_vtoce[0x17] = PPO_$NIL_ORG_UID.low;

    /* Set default ACL data at offset 0x68 */
    new_vtoce[0x1A] = default_acl_data[0];
    new_vtoce[0x1B] = default_acl_data[1];
    new_vtoce[0x1C] = default_acl_data[2];

    /* Set ACL mode bytes to 0x10 each at offset 0x60-0x62 */
    ((uint8_t *)new_vtoce)[0x60] = 0x10;
    ((uint8_t *)new_vtoce)[0x61] = 0x10;
    ((uint8_t *)new_vtoce)[0x62] = 0x10;
    ((uint8_t *)new_vtoce)[0x63] = 0;

    /* Copy parent UID from old offset 0x14 to new offset 0x88 */
    new_vtoce[0x22] = *(uint32_t *)(old_vtoce + 0x14);
    new_vtoce[0x23] = *(uint32_t *)(old_vtoce + 0x18);

    /* Set bit 0 of parent UID low byte */
    *(uint8_t *)(new_vtoce + 0x23) |= 1;

    /* Extract bit 9 from old status -> ext_flags bit 7 (at offset 0x65) */
    old_status = *(uint16_t *)(old_vtoce + 2);
    ((uint8_t *)new_vtoce)[0x65] &= 0x7F;
    if (old_status & 0x0200) {
        ((uint8_t *)new_vtoce)[0x65] |= 0x80;
    }
}
