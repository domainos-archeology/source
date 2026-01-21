/*
 * smd/copy_font_to_md_hdm.c - Copy font to main display hidden memory
 *
 * Copies font data to a fixed location in the main display's hidden
 * memory area. This is used for mono display types (landscape and portrait)
 * to store a default system font at boot time.
 *
 * Unlike SMD_$COPY_FONT_TO_HDM which allocates space dynamically, this
 * function copies to a predetermined location based on display type.
 *
 * Original address: 0x00E1D750
 */

#include "smd/smd_internal.h"

/*
 * Fixed font copy parameters.
 * The font occupies a 39x14 character cell area in HDM.
 */
#define SMD_MD_FONT_ROWS        0x27    /* 39 rows (0 to 0x26) */
#define SMD_MD_FONT_COLS        0x0E    /* 14 columns (0 to 0x0D) */
#define SMD_MD_FONT_COL_START   0x32    /* Starting column offset (word index) */

/* Row offsets for different display types */
#define SMD_MD_LANDSCAPE_ROW    0x01    /* Starting row for landscape (type 1) */
#define SMD_MD_PORTRAIT_ROW     0x3D8   /* Starting row for portrait (type 2) */

/*
 * SMD_$COPY_FONT_TO_MD_HDM - Copy font to main display HDM
 *
 * Copies a fixed-size font bitmap to the main display's hidden memory.
 * The destination depends on the display type:
 *   - Type 1 (mono landscape): rows 1-39, columns 50-63
 *   - Type 2 (mono portrait): rows 984-1022, columns 50-63
 *
 * Parameters:
 *   font_ptr   - Pointer to pointer to font data
 *   status_ret - Status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display associated
 *   status_$display_unsupported_font_version - Invalid font pointer
 *
 * Notes:
 *   - Acquires display lock during copy
 *   - Only handles display types 1 and 2 (mono displays)
 *   - Font data is copied as 16-bit words
 */
void SMD_$COPY_FONT_TO_MD_HDM(void **font_ptr, status_$t *status_ret)
{
    uint16_t unit;
    uint16_t asid;
    uint8_t *unit_base;
    smd_display_hw_t *hw;
    uint16_t *font_data;
    uint32_t display_base;
    int16_t row, col;
    int16_t font_offset;
    int16_t start_row;
    uint16_t *dst;

    *status_ret = status_$ok;

    /* Validate font pointer */
    if (*font_ptr == NULL) {
        *status_ret = status_$display_unsupported_font_version;
        return;
    }

    /* Get current process's ASID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* Acquire display lock */
    SMD_$ACQ_DISPLAY(NULL);

    /* Calculate unit base address */
    unit_base = ((uint8_t *)SMD_DISPLAY_UNITS) + (uint32_t)unit * SMD_DISPLAY_UNIT_SIZE;

    /* Get hardware info pointer */
    hw = *(smd_display_hw_t **)(unit_base - 0xF4);

    /* Get display base from unit data */
    display_base = *(uint32_t *)(unit_base + 0x14);

    /* Get font data pointer */
    font_data = (uint16_t *)*font_ptr;

    /*
     * Copy based on display type.
     * The copy is organized as a 39-row x 14-column block of 16-bit words.
     */
    if (hw->display_type == SMD_DISP_TYPE_MONO_LANDSCAPE) {
        start_row = SMD_MD_LANDSCAPE_ROW;
    } else if (hw->display_type == SMD_DISP_TYPE_MONO_PORTRAIT) {
        start_row = SMD_MD_PORTRAIT_ROW;
    } else {
        /* Other display types not supported for this function */
        SMD_$REL_DISPLAY();
        return;
    }

    /*
     * Copy loop: 39 rows x 14 columns.
     * Each row: copy 14 consecutive 16-bit words.
     * Scanline stride: 0x80 bytes (64 words).
     */
    font_offset = 0;
    for (row = 0; row <= SMD_MD_FONT_ROWS; row++) {
        /* Calculate destination for this row */
        dst = (uint16_t *)(display_base + (start_row + row) * 0x80);
        dst += SMD_MD_FONT_COL_START;

        /* Copy 14 columns for this row */
        for (col = 0; col <= SMD_MD_FONT_COLS; col++) {
            dst[col] = font_data[font_offset + col];
        }

        /* Advance font data by one row (14 words = 0x1C bytes) */
        font_offset += SMD_MD_FONT_COLS + 1;
    }

    SMD_$REL_DISPLAY();
}
