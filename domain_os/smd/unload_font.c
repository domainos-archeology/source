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
    smd_font_entry_t *font_table;
    smd_font_v1_t *font;
    uint16_t hdm_size;

    /* 0x00e6dd2a-0x00e6dd36 */
    asid = PROC1_$AS_ID;
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        /* 0x00e6dd3c */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    slot = *slot_ptr;

    /* 0x00e6dd44-0x00e6dd4c: slot 0 and slots above 8 are "not loaded"
     * (the second test is bhi, i.e. unsigned). */
    if (slot == 0 || slot > SMD_MAX_FONTS_PER_UNIT) {
        *status_ret = status_$display_font_not_loaded;
        return;
    }

    /* 0x00e6dd50-0x00e6dd60: the font table is the record's +0xF4 field. */
    font_table = smd_$unit_rec((int16_t)unit)->font_table;

    /* 0x00e6dd66 */
    if (font_table[slot - 1].font_ptr == NULL) {
        *status_ret = status_$display_font_not_loaded;
        return;
    }

    font = (smd_font_v1_t *)font_table[slot - 1].font_ptr;

    /*
     * 0x00e6dd78-0x00e6dd94: version 3 keeps its HDM size at +0x42, every
     * other version at +0x06, and the *address* of that word is the first
     * argument to SMD_$FREE_HDM - the size is not copied into a local.
     */
    if (font->version == SMD_FONT_VERSION_3) {
        hdm_size = *(uint16_t *)((uint8_t *)font + 0x42);
    } else {
        hdm_size = font->hdm_size;
    }

    /* 0x00e6dd94: FREE_HDM(size, pos, status); the position is the font table
     * entry's own hdm_pos field ("pea (-0x4,A3)"). */
    SMD_$FREE_HDM(&hdm_size, &font_table[slot - 1].hdm_pos, status_ret);

    /* 0x00e6dd98 */
    font_table[slot - 1].font_ptr = NULL;

    /*
     * NOTE: the original does not clear the status here - SMD_$FREE_HDM's
     * status is what the caller sees.
     */
}
