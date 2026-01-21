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

/* Display unit query parameters */
static uint16_t disp_unit_1 = 1;
static uint16_t disp_unit_2 = 2;

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
void SMD_$DISPLAY_LOGO(uint16_t *unit_ptr, int32_t **logo_data, status_$t *status_ret)
{
    int8_t valid;
    uint16_t disp_type;
    uint16_t unit_slot;
    int16_t row;
    int16_t col;
    uint16_t *src;
    uint16_t *dst;
    uint32_t display_base;
    uint32_t dst_offset;
    uint32_t row_offset;

    /* Validate display unit */
    valid = smd_$validate_unit(*unit_ptr);
    if (valid >= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    *status_ret = status_$ok;

    /* Query display type for unit 1 */
    disp_type = SMD_$INQ_DISP_TYPE(&disp_unit_1);
    unit_slot = 1;

    /* If unit 1 has no display, try unit 2 */
    if (disp_type == 0) {
        disp_type = SMD_$INQ_DISP_TYPE(&disp_unit_2);
        unit_slot = 2;

        /* If neither unit has a display, nothing to do */
        if (disp_type == 0) {
            return;
        }
    }

    /* Get display base address from display unit structure */
    display_base = SMD_DISPLAY_UNITS[unit_slot].mapped_addresses[0];

    /* Copy logo to display memory row by row */
    for (row = 0; row <= LOGO_HEIGHT - 1; row++) {
        for (col = 0; col < LOGO_WIDTH / 2; col++) {  /* 32 words per row */
            /* Determine destination offset based on display type */
            if ((1 << (disp_type & 0x1F)) & DISP_TYPE_HIRES_MASK) {
                /* High-res display (2048 wide) */
                row_offset = row * HIRES_BYTES_PER_ROW;
                dst_offset = LOGO_OFFSET_HIRES + row_offset + (col * 2);
            } else {
                /* Standard display (1024 wide) */
                row_offset = row * STD_BYTES_PER_ROW;
                dst_offset = LOGO_OFFSET_STD + row_offset + (col * 2);
            }

            /* Calculate source and destination pointers */
            src = (uint16_t *)((*logo_data) + (row * (LOGO_WIDTH / 4)));
            dst = (uint16_t *)(display_base + dst_offset);

            /* Copy word */
            dst[0] = src[col];
        }
    }
}
