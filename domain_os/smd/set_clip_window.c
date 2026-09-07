/*
 * smd/set_clip_window.c - SMD_$SET_CLIP_WINDOW implementation
 *
 * Sets the clipping window for drawing operations on the display bound to the
 * calling process, clamping it to the display's own bounds.
 *
 * Original address: 0x00E6FE30
 *
 * Assembly (verified with gsk analyze 0x00E6FE30):
 *   00e6fe30    link.w A6,-0x8
 *   00e6fe34    movem.l {  A5 A2 D2},-(SP)
 *   00e6fe38    lea (0xe82b8c).l,A5           ; A5 = SMD_GLOBALS
 *   00e6fe3e    movea.l (0x8,A6),A0           ; A0 = clip_rect
 *   00e6fe42    move.w (0x00e2060a).l,D0w     ; PROC1_$AS_ID
 *   00e6fe48    movea.l (0xc,A6),A1           ; A1 = status_ret
 *   00e6fe4c    add.w D0w,D0w
 *   00e6fe4e    move.w (0x48,A5,D0w*0x1),D0w  ; unit = asid_to_unit[asid]
 *   00e6fe52    bne.b 0x00e6fe5c
 *   00e6fe54    move.l #0x130004,(A1)         ; invalid use of driver procedure
 *   00e6fe5a    bra.b 0x00e6feb4
 *   00e6fe5c    clr.l (A1)                    ; *status_ret = status_$ok
 *   00e6fe5e    move.w D0w,D1w
 *   00e6fe60    movea.l #0xe27376,A2
 *   00e6fe66    ext.l D1                      ; signed
 *   00e6fe68    lsl.l #0x5,D1
 *   00e6fe6a    move.l D1,D2
 *   00e6fe6c    add.l D2,D2
 *   00e6fe6e    add.l D2,D1                   ; D1 = unit * 0x60
 *   00e6fe70    lea (0x0,A2,D1*0x1),A1        ; A1 = base + unit*0x60
 *   00e6fe74    lea (A0),A2
 *   00e6fe76    move.l (A2)+,(-0xa,A1)        ; entry+0x56,0x58 = rect[0],[1]
 *   00e6fe7a    move.l (A2)+,(-0x6,A1)        ; entry+0x5A,0x5C = rect[2],[3]
 *   00e6fe7e    move.w (-0x12,A1),D1w         ; D1 = entry+0x4E (min_x)
 *   00e6fe82    cmp.w (A0),D1w                ; D1 - rect[0]
 *   00e6fe84    ble.b 0x00e6fe8a
 *   00e6fe86    move.w D1w,(-0xa,A1)          ; entry+0x56 = min_x
 *   00e6fe8a    move.w (-0xe,A1),D1w          ; D1 = entry+0x52 (min_y)
 *   00e6fe8e    cmp.w (0x4,A0),D1w            ; D1 - rect[2]
 *   00e6fe92    ble.b 0x00e6fe98
 *   00e6fe94    move.w D1w,(-0x6,A1)          ; entry+0x5A = min_y
 *   00e6fe98    move.w (-0x10,A1),D1w         ; D1 = entry+0x50 (max_x)
 *   00e6fe9c    cmp.w (0x2,A0),D1w            ; D1 - rect[1]
 *   00e6fea0    bge.b 0x00e6fea6
 *   00e6fea2    move.w D1w,(-0x8,A1)          ; entry+0x58 = max_x
 *   00e6fea6    move.w (-0xc,A1),D1w          ; D1 = entry+0x54 (max_y)
 *   00e6feaa    cmp.w (0x6,A0),D1w            ; D1 - rect[3]
 *   00e6feae    bge.b 0x00e6feb4
 *   00e6feb0    move.w D1w,(-0x4,A1)          ; entry+0x5C = max_y
 *   00e6feb4    movem.l (-0x14,A6),{  D2 A2 A5}
 *   00e6feba    unlk A6
 *   00e6febc    rts
 *
 * Bead source-fqne: A1 is base + unit*0x60 and every displacement is negative,
 * so the entry is base + unit*0x60 - 0x60 (1-based) and the fields touched are
 * offsets 0x4E..0x5D of the record, not 0x0C..0x1B as the old
 * smd_display_info_t claimed.  Those are exactly smd_display_hw_t's min_x /
 * max_x / min_y / max_y and clip_x1 / clip_x2 / clip_y1 / clip_y2, which
 * smd_$write_str_clip_impl reads back at 0x00E70402 (X against +0x56/+0x58)
 * and 0x00E7040E (Y against +0x5A/+0x5C).  The two structs have been unified.
 *
 * The clip_rect argument is therefore ordered {x1, x2, y1, y2}, matching the
 * record's own field order - not {x1, y1, x2, y2}.
 */

#include "smd/smd_internal.h"

/*
 * SMD_$SET_CLIP_WINDOW - Set clipping window
 *
 * Parameters:
 *   clip_rect  - four words: x1, x2, y1, y2
 *   status_ret - Output: status return
 */
void SMD_$SET_CLIP_WINDOW(int16_t *clip_rect, status_$t *status_ret)
{
    uint16_t unit;
    smd_display_info_t *info;

    /* 0x00E6FE42-0x00E6FE4E */
    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    *status_ret = status_$ok;

    info = smd_$unit_info((int16_t)unit);

    /* 0x00E6FE76 / 0x00E6FE7A: two longword stores, so the pairs are
     * (clip_x1, clip_x2) and (clip_y1, clip_y2). */
    info->clip_x1 = clip_rect[0];
    info->clip_x2 = clip_rect[1];
    info->clip_y1 = clip_rect[2];
    info->clip_y2 = clip_rect[3];

    /* 0x00E6FE7E: clamp x1 up to the display's min_x */
    if (info->min_x > clip_rect[0]) {
        info->clip_x1 = info->min_x;
    }

    /* 0x00E6FE8A: clamp y1 up to the display's min_y (note the original does
     * the two *minimum* clamps first, then the two maximum clamps) */
    if (info->min_y > clip_rect[2]) {
        info->clip_y1 = info->min_y;
    }

    /* 0x00E6FE98: clamp x2 down to the display's max_x */
    if (info->max_x < clip_rect[1]) {
        info->clip_x2 = info->max_x;
    }

    /* 0x00E6FEA6: clamp y2 down to the display's max_y */
    if (info->max_y < clip_rect[3]) {
        info->clip_y2 = info->max_y;
    }
}
