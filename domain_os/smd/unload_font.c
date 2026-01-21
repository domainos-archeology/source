/*
 * smd/unload_font.c - Unload font from display memory
 *
 * Unloads a previously loaded font from the display's hidden display
 * memory, freeing the HDM space for other use.
 *
 * Original address: 0x00E6DD18
 */

#include "smd/smd_internal.h"

/*
 * SMD_$UNLOAD_FONT - Unload font
 *
 * Unloads a font from hidden display memory for the current display unit.
 *
 * Parameters:
 *   slot_ptr   - Pointer to font slot number (1-8)
 *   status_ret - Status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display associated
 *   status_$display_font_not_loaded - Invalid slot or no font in slot
 */
void SMD_$UNLOAD_FONT(uint16_t *slot_ptr, status_$t *status_ret)
{
    uint16_t unit;
    uint16_t asid;
    uint16_t slot;
    uint8_t *unit_base;
    smd_font_entry_t *font_table;
    smd_font_v1_t *font;
    smd_hdm_pos_t hdm_pos;
    uint16_t hdm_size;

    /* Get current process's ASID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    slot = *slot_ptr;

    /* Validate slot number (1-8) */
    if (slot == 0 || slot > SMD_MAX_FONTS_PER_UNIT) {
        *status_ret = status_$display_font_not_loaded;
        return;
    }

    /* Calculate unit base address */
    unit_base = ((uint8_t *)SMD_DISPLAY_UNITS) + (uint32_t)unit * SMD_DISPLAY_UNIT_SIZE;

    /* Get font table pointer */
    font_table = *(smd_font_entry_t **)unit_base;

    /* Check if font is actually loaded in this slot */
    if (font_table[slot - 1].font_ptr == NULL) {
        *status_ret = status_$display_font_not_loaded;
        return;
    }

    font = (smd_font_v1_t *)font_table[slot - 1].font_ptr;

    /*
     * Get HDM size based on font version to free the correct amount.
     * Version 1: size at offset 0x06
     * Version 3: size at offset 0x42
     */
    if (font->version == SMD_FONT_VERSION_3) {
        hdm_size = *(uint16_t *)((uint8_t *)font + 0x42);
    } else {
        /* Version 1 */
        hdm_size = font->hdm_size;
    }

    /* Reconstruct HDM position for freeing */
    hdm_pos.y = font_table[slot - 1].hdm_offset;
    hdm_pos.x = 0;  /* x coordinate stored elsewhere or derived */

    /* Free the HDM space */
    SMD_$FREE_HDM(&hdm_pos, &hdm_size, status_ret);

    /* Clear the font table entry */
    font_table[slot - 1].font_ptr = NULL;

    *status_ret = status_$ok;
}
