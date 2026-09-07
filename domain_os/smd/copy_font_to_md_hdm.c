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
/* 0x00E1D7CA "move.w #0x26,(-0x22,A6)" plus "subq.w #0x1" / "bcc" gives 39
 * iterations; 0x00E1D7E0 "moveq #0xd,D2" plus dbf gives 14. */
#define SMD_MD_FONT_ROWS        39      /* rows copied */
#define SMD_MD_FONT_COLS        14      /* words per row */
/* 0x00E1D7C2 "move.l #0x10032,(-0xc,A6)": the low half of the longword is the
 * starting word column, the high half the starting scan line. */
#define SMD_MD_FONT_COL_START   0x32    /* Starting column (word index) */
#define SMD_MD_FONT_SRC_STRIDE  0x1C    /* 0x00E1D818 "addi.l #0x1c,D4" */

/* Starting scan line per display type (the high half of the two constants
 * at 0x00E1D7C2 and 0x00E1D826) */
#define SMD_MD_TYPE1_ROW        0x0001  /* display type 1 (800x1024 portrait) */
#define SMD_MD_TYPE2_ROW        0x03D8  /* display type 2 (1024x800 landscape) */

/* Bytes per scan line: 0x00E1D7F2 "lsl.l #0x7,D0" */
#define SMD_MD_SCANLINE_BYTES   0x80

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
    smd_display_unit_t *rec;
    smd_display_hw_t *hw;
    const uint16_t *font_data;
    uint32_t display_base;
    int16_t row, col;
    uint32_t font_offset;
    int16_t start_row;

    /* 0x00e1d760 */
    *status_ret = status_$ok;

    /* 0x00e1d762 */
    if (*font_ptr == NULL) {
        *status_ret = status_$display_unsupported_font_version;
        return;
    }

    /* 0x00e1d770-0x00e1d77e */
    asid = PROC1_$AS_ID;
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        /* 0x00e1d784 */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00e1d78e pea (0x10a,PC) -> 0x00e1d790 + 0x10a = 0x00e1d89a, a word
     * holding 0x0001 (read with gsk) - the same value as SMD_SYNC_LOCK_DATA. */
    SMD_$ACQ_DISPLAY(&SMD_ONE_LOCK_DATA);

    /* 0x00e1d79c-0x00e1d7ac: A1 = 0xE2E3FC + unit*0x10C */
    rec = smd_$unit_rec((int16_t)unit);
    hw = rec->hw;
    display_base = rec->display_base;

    /* 0x00e1d7a6 movea.l (A2),A2: the font pointer is dereferenced once */
    font_data = (const uint16_t *)*font_ptr;

    /* 0x00e1d7b0-0x00e1d7be */
    if (hw->display_type == SMD_DISP_TYPE_MONO_PORTRAIT) {         /* type 1 */
        start_row = SMD_MD_TYPE1_ROW;
    } else if (hw->display_type == SMD_DISP_TYPE_MONO_LANDSCAPE) { /* type 2 */
        start_row = SMD_MD_TYPE2_ROW;
    } else {
        /* 0x00e1d7be: every other type goes straight to the release */
        SMD_$REL_DISPLAY();
        return;
    }

    /*
     * 0x00e1d7dc-0x00e1d822 (and the identical block at 0x00e1d840-0x00e1d886).
     * The source advances 0x1C bytes (14 words) per row while the destination
     * advances one 0x80-byte scan line, starting 0x32 words into the line.
     */
    font_offset = 0;
    for (row = 0; row < SMD_MD_FONT_ROWS; row++) {
        SMD_HW_REG_PTR dst = (SMD_HW_REG_PTR)(uintptr_t)(
            display_base + (uint32_t)(start_row + row) * SMD_MD_SCANLINE_BYTES);

        for (col = 0; col < SMD_MD_FONT_COLS; col++) {
            dst[SMD_MD_FONT_COL_START + col] =
                font_data[font_offset / 2 + (uint32_t)col];
        }

        font_offset += SMD_MD_FONT_SRC_STRIDE;
    }

    /* 0x00e1d888 */
    SMD_$REL_DISPLAY();
}
