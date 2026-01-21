/*
 * smd/load_font.c - Load font into display memory
 *
 * Loads a font into the display's hidden display memory (HDM) for use
 * in text rendering operations. Each display unit can have up to 8
 * fonts loaded simultaneously.
 *
 * The function:
 *   1. Validates the font version (1 or 3)
 *   2. Finds an empty slot in the font table
 *   3. Allocates HDM space for the font
 *   4. Copies the font data to HDM
 *
 * Original address: 0x00E6DC1C
 */

#include "smd/smd_internal.h"

/*
 * SMD_$LOAD_FONT - Load font
 *
 * Loads a font into hidden display memory for the current display unit.
 *
 * Parameters:
 *   font_ptr   - Pointer to pointer to font data
 *   status_ret - Status return
 *
 * Returns:
 *   Font slot number (1-8) on success, undefined on failure
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display associated
 *   status_$display_unsupported_font_version - Font version not 1 or 3
 *   status_$display_internal_font_table_full - All 8 font slots in use
 *   status_$display_hidden_display_memory_full - No HDM space available
 *
 * Notes:
 *   - Font version is at offset 0x00 (version 1 or 3)
 *   - Version 1 HDM size is at offset 0x06
 *   - Version 3 HDM size is at offset 0x42
 *   - Acquires display lock during copy operation
 */
uint16_t SMD_$LOAD_FONT(void **font_ptr, status_$t *status_ret)
{
    uint16_t unit;
    uint16_t asid;
    uint8_t *unit_base;
    smd_font_entry_t *font_table;
    smd_font_v1_t *font;
    uint16_t slot;
    uint16_t hdm_size;
    smd_hdm_pos_t hdm_pos;
    uint32_t display_base;

    /* Get current process's ASID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return 0;
    }

    /* Validate font pointer */
    if (*font_ptr == NULL) {
        *status_ret = status_$display_unsupported_font_version;
        return 0;
    }

    font = (smd_font_v1_t *)*font_ptr;

    /* Calculate unit base address */
    unit_base = ((uint8_t *)SMD_DISPLAY_UNITS) + (uint32_t)unit * SMD_DISPLAY_UNIT_SIZE;

    /* Get font table pointer (first pointer at unit offset 0x00) */
    font_table = *(smd_font_entry_t **)unit_base;

    /* Validate font version - must be 1 or 3 */
    if (font->version != SMD_FONT_VERSION_1 && font->version != SMD_FONT_VERSION_3) {
        *status_ret = status_$display_unsupported_font_version;
        return 0;
    }

    /*
     * Search for an empty slot in the font table.
     * Slots are numbered 1-8 (1-based indexing).
     * Each entry is 8 bytes (font_ptr + hdm_pos).
     */
    slot = 1;
    while (slot <= SMD_MAX_FONTS_PER_UNIT && font_table[slot - 1].font_ptr != NULL) {
        slot++;
    }

    if (slot > SMD_MAX_FONTS_PER_UNIT) {
        *status_ret = status_$display_internal_font_table_full;
        return 0;
    }

    /*
     * Get HDM size needed based on font version.
     * Version 1: size at offset 0x06 (hdm_size field)
     * Version 3: size at offset 0x42 (after char_map)
     */
    if (font->version == SMD_FONT_VERSION_1) {
        hdm_size = font->hdm_size;
    } else {
        /* Version 3: size is at offset 0x42 */
        hdm_size = *(uint16_t *)((uint8_t *)font + 0x42);
    }

    /* Allocate HDM space for the font */
    SMD_$ALLOC_HDM(&hdm_size, &hdm_pos, status_ret);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Store font pointer in the table */
    font_table[slot - 1].font_ptr = *font_ptr;

    /*
     * Acquire display lock and copy font data to HDM.
     * The lock_data pointer (PC-relative in original) identifies the caller.
     */
    SMD_$ACQ_DISPLAY(NULL);

    /* Get display base address from unit data at offset 0x14 */
    display_base = *(uint32_t *)(unit_base + 0x14);

    /* Copy font bitmap to HDM */
    SMD_$COPY_FONT_TO_HDM(display_base, *font_ptr, &hdm_pos);

    SMD_$REL_DISPLAY();

    /* Store HDM position in font table (combined y and x into single uint32) */
    font_table[slot - 1].hdm_offset = hdm_pos.y;

    *status_ret = status_$ok;
    return slot;
}
