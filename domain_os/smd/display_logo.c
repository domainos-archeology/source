/*
 * smd/display_logo.c - SMD_$DISPLAY_LOGO implementation
 *
 * Displays the Apollo/Domain logo on the screen.
 *
 * Original address: 0x00E701EE
 *
 * The logo is a 150-line bitmap, 64 bytes (512 pixels) wide. It is copied
 * to display memory at different offsets depending on the display type:
 *
 * For display types 5 and 9 (high-res 2048x1024):
 *   - Destination offset: 0x1B520 + y * 0x100
 *   - This places the logo in the center of a 2048-wide display
 *
 * For other display types (1024-wide):
 *   - Destination offset: 0xDA90 + y * 0x80
 *   - This places the logo in the center of a 1024-wide display
 *
 * The logo is copied row by row, 32 words (64 bytes) per row.
 */

#include "smd/smd_internal.h"

/* Logo dimensions */
#define LOGO_HEIGHT     150     /* 0x96 lines */
#define LOGO_WIDTH      64      /* 64 bytes = 32 words per row */

/* Display memory offsets for logo placement */
#define LOGO_OFFSET_HIRES   0x1B520     /* Offset for 2048-wide displays */
#define LOGO_OFFSET_STD     0x0DA90     /* Offset for 1024-wide displays */

/* Bytes per row in display memory */
#define HIRES_BYTES_PER_ROW 0x100       /* 2048 pixels / 8 = 256 bytes */
#define STD_BYTES_PER_ROW   0x80        /* 1024 pixels / 8 = 128 bytes */

/* Display type mask for high-res types (5 and 9: bits 5 and 9 = 0x220) */
#define DISP_TYPE_HIRES_MASK    0x220

/*
 * The two unit numbers are constant words in the code region, passed by
 * reference with pea (d,PC):
 *   0x00E70222 pea (-0x222c,PC) -> 0x00E70224 - 0x222C = 0x00E6DFF8 (0x0001)
 *              - the same cell smd_internal.h calls SMD_SYNC_LOCK_DATA
 *   0x00E70238 pea (0xb6,PC)    -> 0x00E7023A + 0xB6   = 0x00E702F0 (0x0002)
 * (contents read with gsk).
 */
static const uint16_t smd_$logo_unit_2 = 2;   /* 0x00E702F0 */

/*
 * SMD_$DISPLAY_LOGO - Display system logo
 *
 * Copies a bitmap logo to the display at a centered position.
 * The function first tries display unit 1, then unit 2 if unit 1
 * has no valid display type.
 *
 * Parameters:
 *   unit_ptr   - Pointer to display unit number
 *   logo_data  - Pointer to logo bitmap data
 *   status_ret - Status return pointer
 *
 * Returns:
 *   status_$ok on success
 *   status_$display_invalid_unit_number if unit is invalid
 */
void SMD_$DISPLAY_LOGO(uint16_t *unit_ptr, void **logo_data, status_$t *status_ret)
{
    int8_t valid;
    uint16_t disp_type;
    int16_t unit_slot;
    int16_t row;
    int16_t col;
    uint32_t display_base;
    uint32_t src_row;    /* D3, starts at 0x40 */
    uint32_t std_row;    /* D4, starts at 0x80 */
    uint32_t hires_row;  /* (-0x14,A6), starts at 0x100 */

    /* 0x00e70204-0x00e70216 */
    valid = smd_$validate_unit(*unit_ptr);
    if (valid >= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* 0x00e70220 */
    *status_ret = status_$ok;

    /* 0x00e70222-0x00e70230: try unit 1 first */
    disp_type = SMD_$INQ_DISP_TYPE((uint16_t *)&SMD_SYNC_LOCK_DATA);
    unit_slot = 1;

    /* 0x00e70232-0x00e7024c: fall back to unit 2 */
    if (disp_type == 0) {
        disp_type = SMD_$INQ_DISP_TYPE((uint16_t *)&smd_$logo_unit_2);
        unit_slot = 2;

        if (disp_type == 0) {
            return;
        }
    }

    /* 0x00e70290 movea.l (0x14,A0),A4 with A0 = 0x00E2E3FC + unit*0x10C, i.e.
     * the unit record's display memory base (record +0x108). */
    display_base = smd_$unit_rec(unit_slot)->display_base;

    /*
     * 0x00e70260-0x00e702e4.  The three running offsets are pre-advanced by
     * one row before the first iteration (D3 = 0x40, D4 = 0x80 and the local
     * at A6-0x14 = 0x100), and the source is then read at (-0x40,A1,D1), so
     * the *source* row is `row` while the *destination* row is `row + 1`.
     * The row counter starts at 0x95 and is decremented with "subq.w #1" plus
     * "bcc", which gives 150 iterations.
     */
    src_row = 0x40;
    std_row = STD_BYTES_PER_ROW;
    hires_row = HIRES_BYTES_PER_ROW;

    for (row = 0; row < LOGO_HEIGHT; row++) {
        for (col = 0; col < LOGO_WIDTH / 2; col++) {  /* moveq #0x1f -> 32 */
            const uint16_t *src;
            SMD_HW_REG_PTR dst;

            /* 0x00e70280-0x00e70288 "btst.l D5,D6" with D6 = 0x220, i.e. the
             * display type is 5 or 9 (the 2048-wide displays). */
            if (((DISP_TYPE_HIRES_MASK >> (disp_type & 0x1F)) & 1u) != 0) {
                dst = (SMD_HW_REG_PTR)(uintptr_t)(display_base + hires_row +
                                                  (uint32_t)(uint16_t)(col * 2) +
                                                  LOGO_OFFSET_HIRES);
            } else {
                dst = (SMD_HW_REG_PTR)(uintptr_t)(display_base + std_row +
                                                  (uint32_t)(uint16_t)(col * 2) +
                                                  LOGO_OFFSET_STD);
            }

            /* 0x00e702c0 move.w (-0x40,A1,D1*0x1),(A4): both sides are raster
             * words, so this is a 16-bit element copy, not a scalar. */
            src = (const uint16_t *)*logo_data;
            *dst = src[(src_row - 0x40) / 2 + (uint32_t)col];
        }

        /* 0x00e702cc-0x00e702da */
        std_row += STD_BYTES_PER_ROW;
        hires_row += HIRES_BYTES_PER_ROW;
        src_row += 0x40;
    }
}
