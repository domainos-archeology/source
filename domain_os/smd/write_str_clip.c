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
 * Re-emitted from the image (2026-09-27): the body is smd_$write_str_clip_impl
 * at 0x00E70390..0x00E706BC (816 bytes), reached through the 4-byte gate at
 * 0x00E8493E.  The movem of ten registers precedes the `link.w A6,-0x2a`, so
 * the arguments sit at (0x30,A6) pos, (0x34) font slot ptr, (0x38) buffer ->
 * A4, (0x3C) length ptr, (0x40) flags ptr, (0x44) status_ret.  Locals:
 * A6-0x1C the smd_str_init_result_t; -0x1E src_x, -0x20 dst_y, -0x22
 * bit_pos, -0x26 x_extent, -0x28 dst_x, -0x2A clip_left.  Registers: D7 the
 * dbf counter, D4 x, D5 y + 1, D3 the BLT control word, A2 font, A3 the BLT
 * registers, A0 the hardware record (clip window at +0x56..+0x5C), A1 the
 * glyph record.
 *
 * Shape of the loop (0x00E7041A..0x00E7061E), per character:
 *   - map the character to a glyph; index 0 -> 0x00E706A2 adds the font's
 *     default_missing + char_spacing and goes to the dbf;
 *   - X: glyph_x = x - bearing_x; clip_left = clip_x1 - glyph_x (0 when
 *     not positive); a glyph already right of clip_x2 either ADVANCES
 *     (x - 0x7F <= clip_x2) or STOPS drawing (0x00E70620: the rest of the
 *     string is only measured);
 *   - right edge = x + width - 1; left of clip_x1 -> advance; clamped to
 *     clip_x2; bit_pos = edge & 0xF; x_extent = -|edge>>4 - dst_x>>4| - 1;
 *   - Y: the same shape with bearing_y / clip_y1 / clip_y2 and the font's
 *     descent (v1 +0x16, v3 +0x48) deciding advance vs stop;
 *   - the HDM source row (hdm.y + bitmap_row + clip_top) is folded into
 *     0..0x3FF by stepping down 0xE0 rows and right 0xE0 columns
 *     (0x00E70534..0x00E70544);
 *   - bottom = y + height - 1; above clip_y1 -> the font's ascent (v1
 *     +0x18, v3 +0x4A) decides stop vs advance; clamped to clip_y2;
 *   - when the glyph crosses the HDM fold (rows to the fold < rows to
 *     draw) TWO BLTs are issued, the second from HDM row 0x320 with the
 *     source column advanced by 0xE0 (0x00E70598..0x00E705EE); otherwise
 *     one (0x00E705F0..0x00E70600);
 *   - x += advance + char_spacing (v1 +0x10, v3 +0x5A); dbf D7.
 * Afterwards (0x00E7068C) the BLT engine is waited for, D4 is parked on the
 * stack across SMD_$REL_DISPLAY (reached through the context table's +0x1C)
 * and popped into D0: the procedure leaves the string's width in D0, which
 * no caller reads.
 *
 * The old body had none of the fold handling, no split BLT and no stop
 * path, and computed the extents differently.
 *
 * Parameters:
 *   pos        - pointer to the packed position (x low word, y high word)
 *   font       - pointer to the font SLOT word
 *   buffer     - the characters
 *   length     - pointer to the count word
 *   flags      - pointer to a Domain boolean: negative = inverted
 *   status_ret - the record's status
 */
void SMD_$WRITE_STR_CLIP(uint32_t *pos, void *font, uint8_t *buffer,
                         uint16_t *length, int8_t *flags, status_$t *status_ret)
{
    smd_str_init_result_t r;             /* A6-0x1C */
    smd_hw_blt_regs_t *blt;              /* A3 */
    smd_font_v1_t *fnt;                  /* A2 */
    smd_font_v3_t *fnt3;
    smd_display_hw_t *hw;                /* A0 */
    smd_glyph_metrics_t *g;              /* A1 */
    const uint8_t *buf;                  /* A4 */
    int16_t chars;                       /* D7 */
    uint16_t rop;                        /* D3 */
    int16_t x;                           /* D4 */
    int16_t y;                           /* D5 */
    int16_t d0, d1, d2, d6;
    int16_t clip_left, dst_x, bit_pos, x_extent, dst_y, src_x;
    uint8_t c;
    uint8_t idx;

    /* 0x00E7039A-0x00E703AC */
    r.font_slot = *(const uint16_t *)font;
    SMD_$WS_INIT((struct smd_ws_ctx_t *)&r);

    /* 0x00E703AE-0x00E703B6 */
    *status_ret = r.status;
    if (r.status != status_$ok) {
        return;
    }

    /* 0x00E703BA-0x00E703D0 */
    blt = r.blt_regs;
    fnt = (smd_font_v1_t *)r.font;
    fnt3 = (smd_font_v3_t *)r.font;
    buf = buffer;
    chars = (int16_t)*length;
    if (chars <= 0) {
        return;
    }
    chars--;

    /* 0x00E703D2-0x00E703EE */
    rop = (uint16_t)(SMD_$ACQ_DISPLAY(*flags < 0 ? &smd_$ws_acq_lock_mode
                                                 : &smd_$ws_one_lock_mode) | 0x800C);

    /* 0x00E703F2-0x00E703FC */
    x = (int16_t)(*pos & 0xFFFF);
    y = (int16_t)((*pos >> 16) + 1);

    /* 0x00E703FE-0x00E70416 */
    hw = r.hw;
    if (hw->clip_x1 > hw->clip_x2) {
        goto measure;
    }
    if (hw->clip_y1 > hw->clip_y2) {
        goto measure;
    }

draw_loop:
    /* 0x00E7041A-0x00E70452: character -> glyph record */
    if (fnt->version != SMD_FONT_VERSION_1) {
        c = *buf++;
        idx = ((const uint8_t *)fnt3)[fnt3->char_map_offset + c];      /* 0x00E70426 */
        if (idx == 0) {
            goto missing;
        }
        g = (smd_glyph_metrics_t *)((uint8_t *)fnt3 - 8 + fnt3->glyph_data_offset + idx * 8);
    } else {
        c = (uint8_t)(*buf++ & 0x7F);                                   /* 0x00E7043E */
        idx = fnt->char_map[c];
        if (idx == 0) {
            goto missing;
        }
        g = (smd_glyph_metrics_t *)((uint8_t *)fnt + 0x92 + idx * 8);
    }

    /* 0x00E70454-0x00E7047E: X */
    d1 = g->bearing_x;
    d2 = d1;
    d0 = (int16_t)(x - d1);                          /* glyph_x */
    d1 = (int16_t)(hw->clip_x1 - d0);
    if (d1 <= 0) {
        if (d0 <= hw->clip_x2) {
            d1 = 0;                                  /* 0x00E7047E */
        } else {
            d0 = (int16_t)(x - 0x7F);                /* 0x00E7046C */
            if (d0 > hw->clip_x2) {
                goto stop;                           /* 0x00E70476 */
            }
            goto advance;                            /* 0x00E7047A */
        }
    }

    /* 0x00E70482-0x00E704D8 */
    clip_left = d1;
    d0 = (int16_t)(d0 + d1);                         /* dst_x */
    while ((int16_t)blt->control < 0) {
    }
    blt->x_start = (uint16_t)d0;
    dst_x = d0;
    d1 = (int16_t)(g->width - 1);
    d2 = (int16_t)(d2 + d1);
    d0 = (int16_t)(d0 + d2);
    d0 = (int16_t)(d0 - clip_left);                  /* right edge = x + width - 1 */
    if (d0 < hw->clip_x1) {
        goto advance;                                /* 0x00E704A8 */
    }
    if (d0 > hw->clip_x2) {
        d0 = hw->clip_x2;                            /* 0x00E704B2 */
    }
    d2 = (int16_t)(d0 & 0xF);
    blt->bit_pos = (uint16_t)d2;
    bit_pos = d2;
    d0 = (int16_t)((uint16_t)d0 >> 4);
    d2 = (int16_t)((uint16_t)dst_x >> 4);
    d0 = (int16_t)(d0 - d2);
    if (d0 >= 0) {                                   /* blt skips the neg */
        d0 = (int16_t)-d0;
    }
    d0--;
    blt->x_extent = (uint16_t)d0;
    x_extent = d0;

    /* 0x00E704DC-0x00E70512: Y */
    d1 = g->bearing_y;
    d0 = (int16_t)(y - d1);                          /* glyph_y */
    d1 = (int16_t)(hw->clip_y1 - d0);
    if (d1 <= 0) {
        if (d0 <= hw->clip_y2) {
            d1 = 0;                                  /* 0x00E70512 */
        } else {
            d0 = (int16_t)(y - (fnt->version == SMD_FONT_VERSION_1
                                    ? fnt->descent : fnt3->descent));   /* 0x00E704F4 */
            if (d0 > hw->clip_y2) {
                goto stop;                           /* 0x00E7050A */
            }
            goto advance;                            /* 0x00E7050E */
        }
    }

    /* 0x00E70516-0x00E70552 */
    d0 = (int16_t)(d0 + d1);                         /* dst_y */
    blt->y_start = (uint16_t)d0;
    dst_y = d0;
    /* the record's +0x14 longword is the hdm position: y in its high word
     * (A6-0x8), x in its low word (A6-0x6) */
    d2 = (int16_t)((uint16_t)g->bitmap_col + (uint16_t)(r.hdm_pos & 0xFFFF));   /* 0x00E70520 */
    d0 = (int16_t)((uint16_t)(r.hdm_pos >> 16) + g->bitmap_row + d1);          /* 0x00E7052A */
    for (;;) {                                       /* 0x00E70534 */
        d6 = (int16_t)(0x3FF - d0);
        if (d6 >= 0) {
            break;
        }
        d0 = (int16_t)(d0 - 0xE0);
        d2 = (int16_t)(d2 + 0xE0);
    }
    d2 = (int16_t)(d2 + clip_left);                  /* 0x00E70546 */
    blt->pattern = (uint16_t)d2;                     /* src column */
    src_x = d2;
    blt->mask = (uint16_t)d0;                        /* src row */

    /* 0x00E70556-0x00E7058A: bottom edge */
    d2 = (int16_t)(g->height + y - 1);
    if (d2 < hw->clip_y1) {
        d2 = (int16_t)((fnt->version == SMD_FONT_VERSION_1
                            ? fnt->ascent : fnt3->ascent) + y);         /* 0x00E70566 */
        if (d2 < hw->clip_y1) {
            goto stop;                               /* 0x00E7057C */
        }
        goto advance;                                /* 0x00E70580 */
    }
    if (d2 > hw->clip_y2) {
        d2 = hw->clip_y2;                            /* 0x00E7058A */
    }
    d0 = dst_y;
    d2 = (int16_t)(d2 - d0);                         /* rows to draw - 1 */
    if (d6 >= d2) {
        /* 0x00E705F0-0x00E70600: one BLT */
        d0 = (int16_t)(d0 + d2);
        d0 = (int16_t)(d0 - dst_y);
        if (d0 >= 0) {
            d0 = (int16_t)-d0;
        }
        d0--;
        blt->y_extent = (uint16_t)d0;
        blt->control = rop;
    } else {
        /* 0x00E70598-0x00E705EE: the glyph crosses the HDM fold */
        d0 = (int16_t)(d0 + d6);
        d0 = (int16_t)(d0 - dst_y);
        if (d0 >= 0) {
            d0 = (int16_t)-d0;
        }
        d0--;
        blt->y_extent = (uint16_t)d0;
        d0 = (int16_t)(dst_y + d6);
        blt->control = rop;                          /* first BLT */
        d2 = (int16_t)(d2 + d0);
        d2 = (int16_t)(d2 - d6);
        d0++;
        while ((int16_t)blt->control < 0) {
        }
        blt->y_start = (uint16_t)d0;
        d2 = (int16_t)(d2 - d0);
        if (d2 >= 0) {
            d2 = (int16_t)-d2;
        }
        d2--;
        blt->y_extent = (uint16_t)d2;
        blt->mask = 0x320;                           /* 0x00E705CA */
        blt->pattern = (uint16_t)(src_x + 0xE0);     /* 0x00E705D0 */
        blt->bit_pos = (uint16_t)bit_pos;
        blt->x_start = (uint16_t)dst_x;
        blt->x_extent = (uint16_t)x_extent;
        blt->control = rop;                          /* 0x00E70600: second BLT */
    }

advance:
    /* 0x00E70602-0x00E7061E */
    d0 = (int16_t)(g->advance + (fnt->version == SMD_FONT_VERSION_1
                                     ? fnt->char_spacing : fnt3->char_spacing));
    x = (int16_t)(x + d0);
    if (chars-- != 0) {                              /* dbf D7 */
        goto draw_loop;
    }
    goto done;

missing:
    /* 0x00E706A2-0x00E706BC */
    if (fnt->version == SMD_FONT_VERSION_1) {
        x = (int16_t)(x + fnt->default_missing);
        x = (int16_t)(x + fnt->char_spacing);
    } else {
        x = (int16_t)(x + fnt3->default_missing);
        x = (int16_t)(x + fnt3->char_spacing);
    }
    if (chars-- != 0) {
        goto draw_loop;
    }
    goto done;

stop:
    /* 0x00E70620: the current glyph's advance, then measure the rest */
    if (fnt->version == SMD_FONT_VERSION_1) {
        goto v1_measure_glyph;
    }
    goto v3_measure_glyph;

measure:
    /* 0x00E70628: the clip window is degenerate -- only measure */
    if (fnt->version == SMD_FONT_VERSION_1) {
        goto v1_measure;
    }

v3_measure:
    /*
     * 0x00E7062E-0x00E7065C.  ORIGINAL BUG, PRESERVED (bead source-2gs7):
     * `move.b (0x34,A2,D0w*0x1),D1b` indexes the character map AT +0x34,
     * where +0x34 is the map's OFFSET field -- the drawing path above adds
     * the offset instead (0x00E70426).  Do not "fix" it.
     */
    c = *buf++;
    idx = ((const uint8_t *)&fnt3->char_map_offset)[c];
    if (idx == 0) {
        d0 = (int16_t)fnt3->default_missing;         /* 0x00E7063A */
        goto v3_measure_add;
    }
    g = (smd_glyph_metrics_t *)((uint8_t *)fnt3 - 8 + fnt3->glyph_data_offset + idx * 8);
v3_measure_glyph:
    d0 = g->advance;                                 /* 0x00E7064C */
v3_measure_add:
    d0 = (int16_t)(d0 + fnt3->char_spacing);         /* 0x00E70652 */
    x = (int16_t)(x + d0);
    if (chars-- != 0) {
        goto v3_measure;
    }
    goto done;

v1_measure:
    /* 0x00E70660-0x00E70688 */
    c = (uint8_t)(*buf++ & 0x7F);
    idx = fnt->char_map[c];
    if (idx == 0) {
        d0 = (int16_t)fnt->default_missing;          /* 0x00E7066E */
        goto v1_measure_add;
    }
    g = (smd_glyph_metrics_t *)((uint8_t *)fnt + 0x92 + idx * 8);
v1_measure_glyph:
    d0 = g->advance;                                 /* 0x00E7067C */
v1_measure_add:
    d0 = (int16_t)(d0 + fnt->char_spacing);          /* 0x00E70682 */
    x = (int16_t)(x + d0);
    if (chars-- != 0) {
        goto v1_measure;
    }

done:
    /* 0x00E7068C-0x00E70698: wait for the engine, release, D0 = x (unused) */
    while ((int16_t)blt->control < 0) {
    }
    SMD_$REL_DISPLAY();
}
