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
 * The two constant words in the context table at 0x00E84958 / 0x00E8495A,
 * passed to SMD_$ACQ_DISPLAY by reference (`pea (0x24,A5)` at 0x00E703DA and
 * `pea (0x26,A5)` at 0x00E703E0 with A5 = 0x00E84934).  SMD_$ACQ_DISPLAY does
 * not treat its argument as read-only, so they are not const.
 */
static int16_t smd_$ws_one_lock_mode = 1;   /* 0x00E84958, chosen when the
                                             * inverted flag is clear */
static int16_t smd_$ws_acq_lock_mode = 0;   /* 0x00E8495A, chosen when it is set */

/*
 * The context A0 points at.  The trampoline is `lea (-0xc,PC),A0` at
 * 0x00E8493E, whose effective address is 0x00E84934 - its own address - and
 * the implementation keeps it in A5 (0x00E70394 `movea.l A0,A5`).  The image
 * contents settle every field:
 *
 *   0x00E84934  41fa fffe          lea (-0x2,PC),A0     +0x00
 *   0x00E84938  4ef9 00e702f4      jmp SMD_$COPY_FONT_TO_HDM
 *   0x00E8493E  41fa fff4          lea (-0xc,PC),A0     +0x0A
 *   0x00E84942  4ef9 00e70390      jmp smd_$write_str_clip_impl
 *   0x00E84948  00e6de58           +0x14 SMD_$WS_INIT
 *   0x00E8494C  00e6eb42           +0x18 SMD_$ACQ_DISPLAY
 *   0x00E84950  00e6ec10           +0x1C SMD_$REL_DISPLAY
 *   0x00E84954  00e272bc           +0x20 SMD_$START_BLT
 *   0x00E84958  0001 0000          +0x24 / +0x26, the two lock-mode words
 *
 * So the "callbacks" are ordinary SMD entry points reached indirectly, and
 * +0x24 / +0x26 are the constant words SMD_$ACQ_DISPLAY is handed by
 * reference (0x00E703DA `pea (0x24,A5)` / 0x00E703E0 `pea (0x26,A5)`).
 */

/*
 * The record SMD_$WS_INIT (0x00E6DE58) fills, built at A6-0x1C.  Its +0x18 is
 * an INPUT: the font slot number, copied there from the caller's second
 * argument at 0x00E7039A before the call.
 */
typedef struct smd_str_init_result_t {
    void                *font;         /* 0x00: font_table[slot].font_ptr
                                        *       (0x00E6DE9E) */
    void                *pad_04;       /* 0x04: never written */
    smd_hw_blt_regs_t   *blt_regs;     /* 0x08: the unit record's +0xFC
                                        *       control registers (0x00E6DEBA) */
    smd_display_hw_t    *hw;           /* 0x0C: the unit record's +0x00
                                        *       hardware record (0x00E6DEC0) */
    status_$t           status;        /* 0x10: 0x00130004 no display,
                                        *       0x00130002 font not loaded,
                                        *       else cleared (0x00E6DEC6) */
    uint32_t            hdm_pos;       /* 0x14: font_table[slot].hdm_pos
                                        *       (0x00E6DEAE) */
    uint16_t            font_slot;     /* 0x18: INPUT (0x00E6DE8E) */
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
     * 0x00E7039A: the font SLOT number is copied into the record's +0x18
     * before the call, then 0x00E703A2 `pea (-0x1c,A6)` / 0x00E703A6
     * `movea.l (0x14,A5),A0` / `jsr (A0)` runs SMD_$WS_INIT through the
     * context table.  `font` is a pointer to that WORD, not to a font.
     */
    init_result.font_slot = *(const uint16_t *)font;
    SMD_$WS_INIT((struct smd_ws_ctx_t *)&init_result);

    /*
     * 0x00E703AE-0x00E703B6: the record's status goes straight out, and a
     * non-zero one ends the call.
     */
    *status_ret = init_result.status;
    if (init_result.status != status_$ok) {
        return;
    }

    /* 0x00E703BA / 0x00E703BE. */
    hw = init_result.blt_regs;
    font_ptr = (smd_font_v1_t *)init_result.font;

    /* 0x00E703C6-0x00E703D0: an empty string returns without acquiring. */
    if ((int16_t)*length <= 0) {
        return;
    }
    chars_remaining = (int16_t)*length - 1;

    /*
     * 0x00E703D2-0x00E703EE.  The lock mode is one of the two constant words
     * in the context table - +0x24 holds 1 and +0x26 holds 0 - and the
     * INVERTED flag selects the 0 one.  SMD_$ACQ_DISPLAY's result is OR'd
     * with 0x800C to make the BLT control word.
     */
    rop_mode = (uint16_t)(SMD_$ACQ_DISPLAY(*flags < 0
                                               ? &smd_$ws_acq_lock_mode
                                               : &smd_$ws_one_lock_mode)
                          | 0x800C);

    /* 0x00E703F2-0x00E703FC: X is the low half, Y the high half plus one. */
    x_pos = (int16_t)(*pos & 0xFFFF);
    y_pos = (int16_t)((*pos >> 16) & 0xFFFF) + 1;

    /*
     * 0x00E703FE-0x00E70416: the clip window comes from the record's +0x0C
     * hardware record.  There is no null check, and a degenerate window falls
     * through to the width-measuring arm at 0x00E70628.
     */
    info = init_result.hw;
    clip_x1 = info->clip_x1;
    clip_x2 = info->clip_x2;
    clip_y1 = info->clip_y1;
    clip_y2 = info->clip_y2;

    if (clip_x1 > clip_x2 || clip_y1 > clip_y2) {
        goto skip_rendering;
    }

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
            /* Version 1: 7-bit ASCII, mask high bit (0x00E7043E
             * `move.w #0x7f,D0w` / `and.b (A4)+,D0b`). */
            glyph_idx = font_ptr->char_map[c & 0x7F];
            if (glyph_idx == 0) {
                /* 0x00E706B4: default_missing (+0x12) then char_spacing (+0x10). */
                x_pos += font_ptr->default_missing + font_ptr->char_spacing;
                continue;
            }
            /* 0x00E7044E `lea (0x92,A2),A1` + `adda.w D1w,A1` with
             * D1 = index * 8, i.e. the records are 1-based from 0x9A. */
            glyph = (smd_glyph_metrics_t *)((uint8_t *)font_ptr + 0x92 + glyph_idx * 8);
        } else {
            /* Version 3: full 8-bit lookup through the map OFFSET at +0x34
             * (0x00E70426 `add.l (0x34,A2),D0` / 0x00E7042A
             * `move.b (0x0,A2,D0*0x1),D1b`). */
            smd_font_v3_t *font_v3 = (smd_font_v3_t *)font_ptr;
            const uint8_t *char_map =
                (const uint8_t *)font_v3 + font_v3->char_map_offset;

            glyph_idx = char_map[c];
            if (glyph_idx == 0) {
                /* 0x00E706A8: default_missing (+0x6E) then char_spacing (+0x5A). */
                x_pos += font_v3->default_missing + font_v3->char_spacing;
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
        /*
         * 0x00E70602-0x00E70618: the glyph's advance (+0x04) plus the
         * version's spacing word, v1 +0x10 (`add.w (0x10,A2),D0w`) or
         * v3 +0x5A (`add.w (0x5a,A2),D0w`).
         */
        if (font_ptr->version == SMD_FONT_VERSION_1) {
            x_pos += glyph->advance + font_ptr->char_spacing;
        } else {
            const smd_font_v3_t *font_v3 = (const smd_font_v3_t *)font_ptr;
            x_pos += glyph->advance + font_v3->char_spacing;
        }
    }

    /* 0x00E7068C `tst.w (A3)` / `bmi.b`: wait for the final BLT. */
    while ((int16_t)hw->control < 0) {
        /* Busy wait */
    }

    /*
     * 0x00E70690-0x00E70698.  The accumulated width is parked on the stack
     * across the call and popped back into D0 afterwards, which is why the
     * measure-only arm and this one share the epilogue.  SMD_$REL_DISPLAY is
     * reached through the context table's +0x1C.
     */
    SMD_$REL_DISPLAY();

    return;

skip_rendering:
    /*
     * 0x00E70628: the clip window is degenerate, so nothing is drawn - the
     * loop only accumulates the string's width, and falls into the shared
     * epilogue at 0x00E7068C above.  font_ptr and chars_remaining are already
     * set; the original does not reload them.
     */
    while (chars_remaining >= 0) {
        c = *buffer++;
        chars_remaining--;

        if (font_ptr->version == SMD_FONT_VERSION_1) {
            /* 0x00E70660-0x00E70686. */
            glyph_idx = font_ptr->char_map[c & 0x7F];
            if (glyph_idx == 0) {
                x_pos += font_ptr->default_missing + font_ptr->char_spacing;
            } else {
                glyph = (smd_glyph_metrics_t *)((uint8_t *)font_ptr + 0x92 + glyph_idx * 8);
                x_pos += glyph->advance + font_ptr->char_spacing;
            }
        } else {
            smd_font_v3_t *font_v3 = (smd_font_v3_t *)font_ptr;

            /*
             * ORIGINAL BUG, PRESERVED (bead source-2gs7).  0x00E70634 is
             * `move.b (0x34,A2,D0w*0x1),D1b`: this path indexes the character
             * map at font + 0x34, where +0x34 is the map's OFFSET FIELD, not
             * the map.  The drawing path above adds the offset instead
             * (0x00E70426).  Consequently characters 0..7 read the two offset
             * longwords and 8..255 read whatever follows them in the header,
             * so the width this arm computes is meaningless.  It only runs on
             * the "string does not fit the clip window" path (0x00E7040A /
             * 0x00E70416 branch to 0x00E70628), which measures instead of
             * drawing.  Do not "fix" it.
             */
            const uint8_t *bogus_char_map =
                (const uint8_t *)&font_v3->char_map_offset;

            glyph_idx = bogus_char_map[c];
            if (glyph_idx == 0) {
                /* 0x00E7063A / 0x00E70652. */
                x_pos += font_v3->default_missing + font_v3->char_spacing;
            } else {
                glyph = (smd_glyph_metrics_t *)((uint8_t *)font_ptr +
                        font_v3->glyph_data_offset + (glyph_idx - 1) * 8);
                /* 0x00E7064C / 0x00E70652. */
                x_pos += glyph->advance + font_v3->char_spacing;
            }
        }
    }

    /* 0x00E7065C / 0x00E70688 `bra.w 0x00E7068C`: the two measuring loops
     * rejoin the wait-and-release epilogue. */
    while ((int16_t)hw->control < 0) {
        /* Busy wait */
    }
    SMD_$REL_DISPLAY();
}
