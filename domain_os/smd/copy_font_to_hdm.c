/*
 * smd/copy_font_to_hdm.c - Copy font bitmap data to hidden display memory
 *
 * Copies font bitmap data from system memory to the hidden portion of
 * display memory. The copy process includes XOR'ing with a mask value
 * read from display memory for hardware compatibility.
 *
 * The HDM is organized as scanlines below/beside the visible display area.
 * Font bitmaps are copied row-by-row with wrapping at scanline boundaries.
 *
 * Original addresses:
 *   0x00E84934 - Trampoline (loads A0 then jumps)
 *   0x00E702F4 - Actual implementation
 */

#include "smd/smd_internal.h"

/*
 * Display memory layout constants.
 * The XOR mask is stored at a fixed offset in display memory.
 */
#define SMD_XOR_MASK_OFFSET     0x1FFF0     /* Offset to XOR mask in display mem */
#define SMD_SCANLINE_WORDS      0x80        /* Words per scanline (128 bytes) */
#define SMD_HDM_WRAP_ROW        0x3FF       /* Last row before wrap (1023) */
#define SMD_ROWS_PER_SEGMENT    0xE0        /* Rows per HDM segment (224) */
#define SMD_MAX_WORDS_PER_ITER  7           /* Max 32-bit words copied per row iteration */
#define SMD_WORD_SKIP           0x68        /* Words to skip between row copies (0x1A * 4) */

/*
 * SMD_$COPY_FONT_TO_HDM - Copy font to hidden display memory
 *
 * Copies font bitmap data from system memory to HDM. The bitmap is
 * copied in 32-bit words, XOR'd with a mask value from display memory
 * for hardware compatibility.
 *
 * Parameters:
 *   display_base - Base address of display memory
 *   font         - Pointer to font header (version 1 or 3)
 *   hdm_pos      - Pointer to HDM position (y=start row, x=bit offset)
 *
 * Memory layout:
 *   - Display memory is organized as scanlines of 0x80 words each
 *   - HDM starts at row specified by hdm_pos->y
 *   - XOR mask read from display_base + 0x1FFF0
 *   - Copying wraps at row 0x3FF to row 0 with column offset
 *
 * Algorithm:
 *   - Reads font bitmap data offset from font header
 *   - Calculates total size in 32-bit words
 *   - Iterates copying up to 7 words per row
 *   - XORs each word with the mask
 *   - Wraps to next segment when reaching row 0x3FF
 */
void SMD_$COPY_FONT_TO_HDM(uint32_t display_base, void *font, smd_hdm_pos_t *hdm_pos)
{
    smd_font_v1_t *font_v1 = (smd_font_v1_t *)font;
    uint32_t data_size;
    uint32_t data_offset;
    uint32_t *src;              /* Source: font bitmap data */
    uint32_t *dst;              /* Destination: HDM in display memory */
    uint32_t xor_mask;          /* XOR mask from display memory */
    int16_t rows_remaining;     /* Rows left before wrap */
    int16_t words_remaining;    /* Total 32-bit words to copy */
    int16_t words_this_row;     /* Words to copy this iteration */
    int16_t i;

    /*
     * Get font bitmap data location and size based on version.
     * Version 1: offset at 0x02, size at 0x08 (as word count)
     * Version 3: offset at 0x28, size at 0x2C
     */
    if (font_v1->version == SMD_FONT_VERSION_1) {
        data_offset = font_v1->data_offset;
        data_size = font_v1->char_width;  /* Actually stores size in v1 */
    } else {
        /* Version 3 */
        smd_font_v3_t *font_v3 = (smd_font_v3_t *)font;
        data_offset = font_v3->data_offset;
        data_size = font_v3->data_size;
    }

    /* Calculate source pointer */
    src = (uint32_t *)((uint8_t *)font + data_offset);

    /* Calculate size in 32-bit words, rounding up */
    words_remaining = ((data_size + 3) >> 2) - 1;

    /* Read XOR mask from display memory */
    xor_mask = *(uint32_t *)(display_base + SMD_XOR_MASK_OFFSET);

    /* Calculate initial destination in HDM */
    dst = (uint32_t *)(display_base +
                       (uint32_t)hdm_pos->y * SMD_SCANLINE_WORDS +
                       (hdm_pos->x >> 3));

    /* Calculate rows remaining before wrap */
    rows_remaining = SMD_HDM_WRAP_ROW - hdm_pos->y;

    /*
     * Copy loop: process up to 7 words per row iteration.
     * This matches the original assembly which uses dbf with D2=6.
     */
    do {
        /* Limit words per iteration */
        words_this_row = words_remaining;
        if (words_this_row > (SMD_MAX_WORDS_PER_ITER - 1)) {
            words_this_row = SMD_MAX_WORDS_PER_ITER - 1;
        }

        /* Copy words for this row segment */
        for (i = words_this_row; i >= 0; i--) {
            *dst = *src ^ xor_mask;
            src++;
            dst++;
        }

        /* Advance to next row (skip rest of scanline) */
        dst += SMD_WORD_SKIP / 4;  /* 0x1A words = 0x68 bytes / 4 */

        /* Check for row wrap */
        rows_remaining--;
        if (rows_remaining < 0) {
            /* Wrap: go back by 0x1BDF words and reset row counter */
            dst -= 0x1BDF;
            rows_remaining = SMD_ROWS_PER_SEGMENT - 1;  /* 0xDF = 223 */
        }

        /* Update remaining word count */
        words_remaining -= (SMD_MAX_WORDS_PER_ITER);
    } while (words_remaining >= 0);
}
