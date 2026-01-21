/*
 * smd/write_str_clip.c - Write string with clipping
 *
 * Internal function that renders text to the display with clipping.
 * Handles both version 1 and version 3 fonts, character lookup,
 * glyph metrics, and hardware BLT operations for rendering.
 *
 * This is a trampoline + implementation:
 *   0x00E8493E - Trampoline that sets up A0 and jumps
 *   0x00E70390 - Actual implementation (816 bytes)
 *
 * The function uses a callback-based architecture where function pointers
 * in a context structure (pointed to by A0) are called for:
 *   - Initialization (offset 0x14)
 *   - Font lookup (offset 0x18)
 *   - Cleanup (offset 0x1C)
 *
 * Original addresses: 0x00E8493E (trampoline), 0x00E70390 (implementation)
 */

#include "smd/smd_internal.h"

/*
 * Internal context structure passed via register A0.
 * Contains function pointers for the rendering pipeline.
 */
typedef struct smd_str_context_t {
    uint8_t     pad[0x14];
    void        *(*init_func)(void *);      /* 0x14: Init function */
    void        *(*font_func)(void *);      /* 0x18: Font lookup function */
    void        (*cleanup_func)(void);      /* 0x1C: Cleanup function */
    uint16_t    field_20;                   /* 0x20: Unknown */
    uint16_t    field_22;                   /* 0x22: Unknown */
    uint16_t    rop_normal;                 /* 0x24: Normal ROP mode */
    uint16_t    rop_inverted;               /* 0x26: Inverted ROP mode */
} smd_str_context_t;

/*
 * Internal result structure from init callback.
 */
typedef struct smd_str_init_result_t {
    void                *font;              /* 0x00: Font pointer */
    void                *pad_04;            /* 0x04: Unknown */
    smd_hw_blt_regs_t   *hw_regs;           /* 0x08: Hardware BLT registers */
    smd_display_info_t  *display_info;      /* 0x0C: Display info */
    status_$t           status;             /* 0x10: Status */
} smd_str_init_result_t;

/*
 * SMD_$WRITE_STR_CLIP - Write string with clipping
 *
 * Renders a string using the specified font, clipping each character
 * against the current clip window. Uses hardware BLT operations to
 * transfer glyph bitmaps from HDM to the visible display.
 *
 * Parameters:
 *   pos        - Pointer to position (packed x,y)
 *   font       - Font slot or font pointer
 *   buffer     - Character buffer to render
 *   length     - Pointer to string length
 *   flags      - Rendering flags (bit 7: inverted mode)
 *   status_ret - Status return
 *
 * Notes:
 *   - Complex clipping logic handles partial glyph visibility
 *   - Supports both font version 1 (7-bit ASCII) and version 3 (8-bit)
 *   - Characters outside clip window advance position but don't render
 *   - Unknown characters use default width from font header
 */
void SMD_$WRITE_STR_CLIP(uint32_t *pos, void *font, uint8_t *buffer,
                         uint16_t *length, int8_t *flags, status_$t *status_ret)
{
    smd_str_init_result_t init_result;
    smd_hw_blt_regs_t *hw;
    smd_display_info_t *info;
    smd_font_v1_t *font_ptr;
    smd_glyph_metrics_t *glyph;
    uint16_t rop_mode;
    int16_t x_pos, y_pos;
    int16_t chars_remaining;
    int16_t clip_x1, clip_y1, clip_x2, clip_y2;
    int16_t glyph_x, glyph_y;
    int16_t glyph_idx;
    int16_t src_x, src_y, dst_x, dst_y;
    int16_t width, height;
    int16_t clip_left, clip_top;
    uint8_t c;

    /*
     * In the original code, A0 points to a context structure with callbacks.
     * We simulate this by calling our internal functions directly.
     *
     * The init callback validates the display association and returns
     * font/hardware pointers.
     */

    /* TODO: Call actual init callback - for now simulate the result */
    init_result.status = status_$ok;
    *status_ret = init_result.status;

    if (init_result.status != status_$ok) {
        return;
    }

    if (*length <= 0) {
        return;
    }

    /* Get initial position */
    x_pos = (int16_t)(*pos & 0xFFFF);
    y_pos = (int16_t)((*pos >> 16) & 0xFFFF) + 1;

    /* Get display info and clip bounds */
    info = init_result.display_info;
    if (info == NULL) {
        return;
    }

    clip_x1 = info->clip_x1;
    clip_y1 = info->clip_y1;
    clip_x2 = info->clip_x2;
    clip_y2 = info->clip_y2;

    /* Check if clip window is inverted (degenerate) */
    if (clip_x1 > clip_x2 || clip_y1 > clip_y2) {
        /* Clip window invalid - skip to position-only mode */
        goto skip_rendering;
    }

    /* Get font pointer */
    font_ptr = (smd_font_v1_t *)init_result.font;

    /* Get hardware BLT registers */
    hw = init_result.hw_regs;

    /* Select ROP mode based on flags */
    if (*flags < 0) {
        /* Inverted mode (bit 7 set) */
        rop_mode = 0x800C;  /* Base ROP with inverted pattern */
    } else {
        rop_mode = 0x800C;  /* Normal ROP */
    }

    chars_remaining = *length - 1;

    /*
     * Main character rendering loop.
     * For each character:
     *   1. Look up glyph index from character map
     *   2. Get glyph metrics
     *   3. Calculate screen position
     *   4. Clip against window
     *   5. Issue BLT if visible
     *   6. Advance position
     */
    while (chars_remaining >= 0) {
        c = *buffer++;
        chars_remaining--;

        /* Look up glyph index based on font version */
        if (font_ptr->version == SMD_FONT_VERSION_1) {
            /* Version 1: 7-bit ASCII, mask high bit */
            glyph_idx = font_ptr->char_map[c & 0x7F];
            if (glyph_idx == 0) {
                /* Unknown character - use default width */
                x_pos += font_ptr->char_width + font_ptr->unknown_char_width;
                continue;
            }
            glyph = (smd_glyph_metrics_t *)((uint8_t *)font_ptr + 0x92 + glyph_idx * 8);
        } else {
            /* Version 3: full 8-bit lookup */
            smd_font_v3_t *font_v3 = (smd_font_v3_t *)font_ptr;
            glyph_idx = font_v3->char_map[c];
            if (glyph_idx == 0) {
                x_pos += font_v3->field_0a + font_v3->field_0c;
                continue;
            }
            glyph = (smd_glyph_metrics_t *)((uint8_t *)font_ptr +
                    font_v3->glyph_data_offset + (glyph_idx - 1) * 8);
        }

        /* Calculate glyph screen position */
        glyph_x = x_pos - glyph->bearing_x;
        glyph_y = y_pos - glyph->bearing_y;

        /* Clip left edge */
        clip_left = clip_x1 - glyph_x;
        if (clip_left > 0) {
            if (glyph_x + glyph->width - 1 < clip_x1) {
                /* Entirely to the left of clip window */
                goto advance_position;
            }
        } else {
            clip_left = 0;
        }

        /* Clip right edge */
        dst_x = glyph_x + clip_left;
        width = glyph->width - 1 - clip_left;
        if (dst_x + width > clip_x2) {
            width = clip_x2 - dst_x;
        }

        /* Clip top edge */
        clip_top = clip_y1 - glyph_y;
        if (clip_top > 0) {
            if (glyph_y + glyph->height - 1 < clip_y1) {
                goto advance_position;
            }
        } else {
            clip_top = 0;
        }

        /* Clip bottom edge */
        dst_y = glyph_y + clip_top;
        height = glyph->height - 1 - clip_top;
        if (dst_y + height > clip_y2) {
            height = clip_y2 - dst_y;
        }

        if (width < 0 || height < 0) {
            goto advance_position;
        }

        /* Calculate source position in font bitmap HDM */
        src_x = glyph->bitmap_col + clip_left;
        src_y = glyph->bitmap_row + clip_top;

        /* Wait for previous BLT to complete */
        while ((int16_t)hw->control < 0) {
            /* Busy wait */
        }

        /* Program BLT registers */
        hw->x_start = dst_x;
        hw->y_start = dst_y;
        hw->bit_pos = dst_x & 0x0F;
        hw->x_extent = (dst_x >> 4) - ((dst_x + width) >> 4);
        if (hw->x_extent > 0) {
            hw->x_extent = -hw->x_extent;
        }
        hw->x_extent--;
        hw->y_extent = height;
        hw->mask = src_y;
        hw->pattern = src_x;

        /* Start BLT operation */
        hw->control = rop_mode;

advance_position:
        /* Advance x position by character advance width */
        if (font_ptr->version == SMD_FONT_VERSION_1) {
            x_pos += font_ptr->char_width + glyph->advance;
        } else {
            smd_font_v3_t *font_v3 = (smd_font_v3_t *)font_ptr;
            x_pos += font_v3->field_0a + glyph->advance;
        }
    }

    /* Wait for final BLT to complete */
    while ((int16_t)hw->control < 0) {
        /* Busy wait */
    }

    /* Call cleanup callback */
    /* TODO: actual cleanup call */

    return;

skip_rendering:
    /*
     * Clip window is degenerate - just advance position through string
     * without rendering anything.
     */
    font_ptr = (smd_font_v1_t *)init_result.font;
    chars_remaining = *length - 1;

    while (chars_remaining >= 0) {
        c = *buffer++;
        chars_remaining--;

        if (font_ptr->version == SMD_FONT_VERSION_1) {
            glyph_idx = font_ptr->char_map[c & 0x7F];
            if (glyph_idx == 0) {
                x_pos += font_ptr->char_width + font_ptr->unknown_char_width;
            } else {
                glyph = (smd_glyph_metrics_t *)((uint8_t *)font_ptr + 0x92 + glyph_idx * 8);
                x_pos += font_ptr->char_width + glyph->advance;
            }
        } else {
            smd_font_v3_t *font_v3 = (smd_font_v3_t *)font_ptr;
            glyph_idx = font_v3->char_map[c];
            if (glyph_idx == 0) {
                x_pos += font_v3->field_0a + font_v3->field_0c;
            } else {
                glyph = (smd_glyph_metrics_t *)((uint8_t *)font_ptr +
                        font_v3->glyph_data_offset + (glyph_idx - 1) * 8);
                x_pos += font_v3->field_0a + glyph->advance;
            }
        }
    }
}
