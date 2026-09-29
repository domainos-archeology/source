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
    smd_display_unit_t *rec;
    smd_font_entry_t *font_table;
    smd_font_v1_t *font;
    uint16_t slot;
    uint16_t hdm_size;

    /* 0x00e6dc2e-0x00e6dc3a */
    asid = PROC1_$AS_ID;
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        /* 0x00e6dc40 - the original leaves D0 holding the zero unit number */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return 0;
    }

    /* 0x00e6dc4c "tst.l (A0)": a null font pointer takes the same exit as a
     * bad version. */
    if (*font_ptr == NULL) {
        *status_ret = status_$display_unsupported_font_version;
        return 0;
    }

    font = (smd_font_v1_t *)*font_ptr;

    /* 0x00e6dc50-0x00e6dc66: A2 = 0xE2E3FC + unit*0x10C and the font table is
     * the record's +0xF4 field, read as (A2). */
    rec = smd_$unit_rec((int16_t)unit);
    font_table = rec->font_table;

    /* 0x00e6dc68-0x00e6dc72 */
    if (font->version != SMD_FONT_VERSION_1 && font->version != SMD_FONT_VERSION_3) {
        *status_ret = status_$display_unsupported_font_version;
        return 0;
    }

    /*
     * 0x00e6dc7e-0x00e6dc96: find a free slot.  Slots are 1-based and the
     * entry for slot s is at font_table + s*8 - 8.
     */
    slot = 1;
    while (slot <= SMD_MAX_FONTS_PER_UNIT && font_table[slot - 1].font_va != 0) {
        slot++;
    }

    /* 0x00e6dc98 */
    if (slot > SMD_MAX_FONTS_PER_UNIT) {
        *status_ret = status_$display_internal_font_table_full;
        return 0;
    }

    /* 0x00e6dca6-0x00e6dcbe: version 1 keeps its HDM size at +0x06, version 3
     * at +0x42 (and re-reads the font pointer to get there). */
    if (font->version == SMD_FONT_VERSION_1) {
        hdm_size = font->hdm_size;
    } else {
        hdm_size = *(uint16_t *)((uint8_t *)*font_ptr + 0x42);
    }

    /*
     * 0x00e6dcca-0x00e6dcd4: the position argument is the *font table entry's
     * own* hdm_pos field, so SMD_$ALLOC_HDM fills it in place - the original
     * keeps no local copy.
     */
    SMD_$ALLOC_HDM(&hdm_size, &font_table[slot - 1].hdm_pos, status_ret);

    /* 0x00e6dcdc */
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* 0x00e6dce0 */
    font_table[slot - 1].font_va = ARCH_PTR_TO_VA(*font_ptr);

    /* 0x00e6dce6 pea (-0x3be,PC) -> 0x00e6dce8 - 0x3be = 0x00e6d92a */
    SMD_$ACQ_DISPLAY(&SMD_ONE_LOCK_DATA);

    /* 0x00e6dcf0-0x00e6dcfe: display base from the record's +0x108 field. */
    SMD_$COPY_FONT_TO_HDM(rec->display_base, *font_ptr,
                          &font_table[slot - 1].hdm_pos);

    /* 0x00e6dd08 */
    SMD_$REL_DISPLAY();

    /* 0x00e6dd0c "move.w D2w,D0w": the slot number is the result.  The
     * original does NOT clear the status here - it was already zero, having
     * been checked after SMD_$ALLOC_HDM. */
    return slot;
}
