/*
 * smd/show_cursor.c - SHOW_CURSOR
 *
 * Internal cursor show/update procedure.  Recomputes the cursor's bounding
 * box, hides the cursor when it would overlap a tracking rectangle, and
 * erases/redraws it when anything about the displayed cursor changed.
 *
 * Original address: 0x00E6E1CC, size 652 bytes.
 * Module base (A5) = 0x00E82B8C = SMD_GLOBALS.
 *
 * This is a Pascal *procedure*: no caller reserves a result slot before the
 * call (e.g. smd_$add_trk_rects_internal 0x00E6E57E-0x00E6E58A pushes three
 * addresses and does a plain bsr), so it returns nothing.
 */

#include "smd/smd_internal.h"

/*
 * Constant cells in the code region that the original passes by reference.
 *
 * 0x00E6DFF8: the 16-bit word 0x0001, pushed by `pea (-0x36c,PC)` at
 *             0x00E6E362 and `pea (-0x378,PC)` at 0x00E6E36E as the "line"
 *             argument of SMD_$ACQ_DISPLAY / SMD_$LOCK_DISPLAY.
 * 0x00E6E458: the byte 0xFF, pushed by `pea (0xb8,PC)` at 0x00E6E39E - the
 *             erase flag for SMD_$XOR_CURSOR.
 * 0x00E6E45A: the byte 0x00, pushed by `pea (0x5e,PC)` at 0x00E6E3FA - the
 *             draw flag for SMD_$XOR_CURSOR.
 */
static const int16_t show_cursor_lock_line = 1;      /* 0x00E6DFF8 */
static const boolean show_cursor_erase_flag = true;  /* 0x00E6E458 (0xFF) */
static const boolean show_cursor_draw_flag = false;  /* 0x00E6E45A (0x00) */

/*
 * SHOW_CURSOR - update the displayed cursor
 *
 * Parameters (all by reference, Pascal `var`/`const`):
 *   pos        - packed cursor position; if it equals
 *                SMD_GLOBALS.cursor_pos_sentinel the display's own remembered
 *                position is used instead (0x00E6E24A)
 *   cursor_num - cursor number; -1 means "keep the current one" (0x00E6E256)
 *   blocking   - Domain boolean; true (0xFF, i.e. < 0) means block in
 *                SMD_$ACQ_DISPLAY, false means try SMD_$LOCK_DISPLAY and give
 *                up if it fails (0x00E6E35E)
 */
void SHOW_CURSOR(const uint32_t *pos, const int16_t *cursor_num,
                 const boolean *blocking)
{
    uint32_t local_pos;             /* (-0x8,A6) */
    int16_t local_cursor_num;       /* D6 */
    boolean local_blocking;         /* D5 */
    smd_display_unit_t *cur_rec;    /* (-0x24,A6): A1 - 0xF4 */
    smd_display_hw_t *cur_hw;       /* (-0x28,A6), later A4 */
    smd_display_unit_t *prev_rec;   /* (-0x2c,A6): A3 - 0xF4 */
    smd_display_hw_t *prev_hw;      /* A2 */
    smd_blink_state_t *blink;       /* (-0x34,A6) = 0x00E273D6 */
    const smd_cursor_pattern_t *pattern; /* A0 */
    int16_t pat_width;              /* (-0xc,A6) */
    int16_t pat_height;             /* D0 */
    int16_t x_left;                 /* D4 */
    int16_t x_right;                /* D2 */
    int16_t y_bottom;               /* D3 */
    int16_t y_top;                  /* (-0x12,A6) */
    boolean visible;                /* (-0x1c,A6) */
    boolean draw_result;            /* D0 after the lock is taken */
    int16_t i;                      /* D0 in the dbf loop */
    int16_t rect_count;             /* D1 */

    /*
     * 00e6e1da movea.l (0x8,A6),A0 / move.l (A0),(-0x8,A6)
     * 00e6e1ea move.w (A1),D6w
     * 00e6e1ee move.b (A2),D5b
     */
    local_pos = *pos;
    local_cursor_num = *cursor_num;
    local_blocking = *blocking;

    /* 00e6e1f0 move.w (0x1d98,A5),-(SP) / bsr smd_$validate_unit
     * 00e6e1fa tst.b D0b / bpl -> exit */
    if (smd_$validate_unit((uint16_t)SMD_GLOBALS.default_unit) >= 0) {
        return;
    }

    /* 00e6e200 cmpi.w #-1,(0x1d9c,A5) / bne / move.w (0x1d98,A5),(0x1d9c,A5) */
    if (SMD_GLOBALS.previous_unit == -1) {
        SMD_GLOBALS.previous_unit = SMD_GLOBALS.default_unit;
    }

    /*
     * 00e6e20e-00e6e23a: both records are addressed as
     *   0x00E2E3FC + unit * 0x10C   (muls.w, i.e. a *signed* multiply)
     * and their fields start 0xF4 below that; smd_$unit_rec() performs
     * exactly that computation.
     */
    cur_rec = smd_$unit_rec(SMD_GLOBALS.default_unit);
    cur_hw = cur_rec->hw;
    prev_rec = smd_$unit_rec(SMD_GLOBALS.previous_unit);
    prev_hw = prev_rec->hw;

    /* 00e6e23e move.l #0xe273d6,(-0x34,A6) */
    blink = &SMD_BLINK_STATE;

    /* 00e6e246 cmp.l (0x1d94,A5),D0 / bne / move.l (0x32,A2),(-0x8,A6) */
    if (local_pos == SMD_GLOBALS.cursor_pos_sentinel) {
        local_pos = prev_hw->cursor_pos;
    }

    /* 00e6e256 cmpi.w #-1,D6w / bne / move.w (0x36,A2),D6w */
    if (local_cursor_num == -1) {
        local_cursor_num = prev_hw->cursor_number;
    }

    /* 00e6e260 st (-0x1c,A6) */
    visible = true;

    /* 00e6e264-00e6e276: pattern = SMD_CURSOR_PTABLE[cursor_num] (the index is
     * sign-extended, `ext.l D0`, before the *4 scale). */
    pattern = SMD_CURSOR_PTABLE[local_cursor_num];
    pat_width = pattern->width;    /* 00e6e276 move.w (A0),(-0xc,A6) */
    pat_height = pattern->height;  /* 00e6e27e move.w (0x2,A0),D0w */

    /*
     * 00e6e27a move.w (-0x6,A6),D4w  ; low half of the packed position = X
     * 00e6e282 sub.w (0x4,A0),D4w
     * 00e6e286 bpl / clr.w D4w
     */
    x_left = SMD_POS_X(local_pos) - pattern->hot_x;
    if (x_left < 0) {
        x_left = 0;
    }

    /*
     * 00e6e28a move.w (-0x8,A6),D3w  ; high half of the packed position = Y
     * 00e6e292 add.w (0x6,A0),D3w
     * 00e6e296 move.w (0x54,A4),D1w / cmp.w D1w,D3w / ble / move.w D1w,D3w
     */
    y_bottom = SMD_POS_Y(local_pos) + pattern->hot_y_adj;
    if (y_bottom > cur_hw->max_y) {
        y_bottom = cur_hw->max_y;
    }

    /*
     * 00e6e2a0 move.w D4w,D2w / add.w (-0xc,A6),D2w
     * 00e6e2a6 cmp.w (0x50,A4),D2w / ble
     * 00e6e2ac move.w (0x50,A4),D2w / addq.w #1,D2w
     *          move.w D2w,D4w / sub.w (-0xc,A6),D4w
     */
    x_right = (int16_t)(x_left + pat_width);
    if (x_right > cur_hw->max_x) {
        x_right = (int16_t)(cur_hw->max_x + 1);
        x_left = (int16_t)(x_right - pat_width);
    }

    /*
     * 00e6e2b8 move.w D3w,D1w / sub.w D0w,D1w / move.w D1w,(-0x12,A6)
     * 00e6e2c0 bpl
     * 00e6e2c2 move.w #-1,(-0x12,A6) / move.w D0w,D3w / subq.w #1,D3w
     */
    y_top = (int16_t)(y_bottom - pat_height);
    if (y_top < 0) {
        y_top = -1;
        y_bottom = (int16_t)(pat_height - 1);
    }

    /*
     * Tracking-rectangle scan, under the SMD exclusion lock.
     * 00e6e2cc pea (0xe2e520).l / jsr ML_$EXCLUSION_START
     * 00e6e2da move.w (0xe6,A5),D1w / subq.w #1,D1w / bmi -> done
     * 00e6e2e2 movea.l A5,A1 / move.w D1w,D0w / addq.l #8,A1
     *          (A1 = &SMD_GLOBALS + 8, so (0xe0,A1) is tracking_rects[0].x1)
     */
    ML_$EXCLUSION_START(&ml_$exclusion_t_00e2e520);

    rect_count = (int16_t)(SMD_GLOBALS.tracking_rect_count - 1);
    if (rect_count >= 0) {
        const smd_track_rect_t *rect = &SMD_GLOBALS.tracking_rects[0];

        /* moveq/dbf: rect_count+1 iterations */
        for (i = rect_count;; i--) {
            /*
             * 00e6e2ee cmp.w (0xe6,A0),D1w ; bge -> next   (y_top   vs y2)
             * 00e6e2f4 cmp.w (0xe4,A0),D3w ; blt -> next   (y_bottom vs y1)
             * 00e6e2fa cmp.w (0xe0,A0),D2w ; ble -> next   (x_right vs x1)
             * 00e6e300 cmp.w (0xe2,A0),D4w ; bgt -> next   (x_left  vs x2)
             * 00e6e306 clr.b (-0x1c,A6) / bra -> done
             */
            if (y_top < rect->y2 && y_bottom >= rect->y1 &&
                x_right > rect->x1 && x_left <= rect->x2) {
                visible = false;
                break;
            }
            rect++;
            if (i == 0) {
                break;
            }
        }
    }

    /* 00e6e312 pea (0xe2e520).l / jsr ML_$EXCLUSION_STOP */
    ML_$EXCLUSION_STOP(&ml_$exclusion_t_00e2e520);

    /* 00e6e320 move.w D6w,(0x36,A2) / move.l (-0x8,A6),(0x32,A2) */
    prev_hw->cursor_number = local_cursor_num;
    prev_hw->cursor_pos = local_pos;

    /*
     * 00e6e32a-00e6e34c: nothing to do unless the visibility, the unit, the
     * position or the cursor number differ from what is on the screen.
     */
    if (visible == cur_hw->cursor_visible &&
        SMD_GLOBALS.previous_unit == SMD_GLOBALS.default_unit &&
        local_pos == SMD_GLOBALS.default_cursor_pos &&
        local_cursor_num == SMD_GLOBALS.cursor_button_state) {
        return;
    }

    /* 00e6e350 move.w PROC1_$AS_ID,D0w / add.w D0w,D0w
     *          move.w (0x1d9c,A5),(0x48,A5,D0w*1) */
    SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = (uint16_t)SMD_GLOBALS.previous_unit;

    /* 00e6e35e tst.b D5b / bpl -> try-lock */
    if (local_blocking < 0) {
        SMD_$ACQ_DISPLAY((int16_t *)&show_cursor_lock_line);
    } else {
        /* 00e6e36e pea const / pea (A2) / jsr SMD_$LOCK_DISPLAY
         * 00e6e37c tst.b D0b / bpl -> exit */
        /* The cell itself is read-only in the original; the cast is only
         * needed because SMD_$LOCK_DISPLAY writes element 0x12 of whatever
         * block it is handed on the scroll-done path. */
        if ((int8_t)SMD_$LOCK_DISPLAY(prev_hw,
                                      (int16_t *)&show_cursor_lock_line) >= 0) {
            return;
        }
    }

    /* 00e6e382 st D0b */
    draw_result = true;

    /* 00e6e384 tst.b (0x38,A2) / bpl -> 0x00e6e3c2 */
    if (prev_hw->cursor_visible < 0) {
        /* 00e6e38a movea.l (-0x34,A6),A0 / clr.b (A0) */
        blink->smd_time_com = 0;

        /* 00e6e390 tst.b (0x2,A0) / bpl -> 0x00e6e3ba */
        if (blink->blink_flag < 0) {
            /*
             * Erase the cursor that is currently on the screen.  The last two
             * arguments are pushed as *values* read out of the previous unit's
             * record:
             *   00e6e396 move.l (0x8,A3),-(SP)   -> rec->ctrl_regs   (+0xFC)
             *   00e6e39a move.l (0x14,A3),-(SP)  -> rec->display_base(+0x108)
             * and the first two are the *addresses of the globals*, so the
             * callee is free to update them.
             */
            draw_result = SMD_$XOR_CURSOR(
                &SMD_GLOBALS.cursor_button_state,
                &SMD_GLOBALS.default_cursor_pos,
                &prev_hw->min_x,
                prev_hw,
                &show_cursor_erase_flag,
                prev_rec->display_base,
                prev_rec->ctrl_regs);
        }

        /* 00e6e3ba tst.b D0b / bpl / clr.b (0x38,A2) */
        if (draw_result < 0) {
            prev_hw->cursor_visible = false;
        }
    }

    /*
     * 00e6e3c2 move.b (-0x1c,A6),D1b / move.l (-0x8,A6),D7 / not.b D1b
     * 00e6e3cc cmp.l (0xd0,A5),D7 / seq D4b / and.b D4b,D1b
     * 00e6e3d4 or.b D1b,(0x1744,A5)
     *
     * i.e. remember that a redraw is owed when the cursor became invisible at
     * the position that is currently drawn.
     */
    SMD_GLOBALS.cursor_pending_flag = (boolean)
        (SMD_GLOBALS.cursor_pending_flag |
         ((boolean)~visible &
          (boolean)((local_pos == SMD_GLOBALS.default_cursor_pos) ? 0xFF : 0)));

    /* 00e6e3d8 move.b (-0x1c,A6),D4b / and.b D0b,D4b / bpl -> 0x00e6e444 */
    if ((boolean)(visible & draw_result) < 0) {
        int16_t draw_cursor_num;      /* D2 */
        int16_t draw_cursor_num_var;  /* (-0x18,A6), passed by reference */

        /*
         * 00e6e3e0 move.w (0x36,A4),D2w
         * 00e6e3e4 move.l (0x32,A4),(-0x8,A6)
         * 00e6e3ea move.w D2w,(-0x18,A6)
         */
        draw_cursor_num = cur_hw->cursor_number;
        local_pos = cur_hw->cursor_pos;
        draw_cursor_num_var = draw_cursor_num;

        /*
         * 00e6e3ee-0x00e6e412: same call, but for the current unit and with
         * the *locals* passed by reference, so the callee may rewrite them.
         */
        draw_result = SMD_$XOR_CURSOR(
            &draw_cursor_num_var,
            &local_pos,
            &cur_hw->min_x,
            cur_hw,
            &show_cursor_draw_flag,
            cur_rec->display_base,
            cur_rec->ctrl_regs);

        /* 00e6e416 tst.b D0b / bpl -> 0x00e6e440 */
        if (draw_result < 0) {
            /*
             * 00e6e41a clr.b (0x1744,A5)
             * 00e6e41e move.l (-0x8,A6),(0xd0,A5)   <- re-read, not the copy
             * 00e6e424 move.w D2w,(0xd4,A5)
             * 00e6e428 st (0x38,A4)
             * 00e6e42c seq D4b     (Z comes from the preceding move.w D2w,...)
             * 00e6e42e movea.l (-0x34,A6),A0 / move.b D4b,(A0)
             * 00e6e434 st (0x2,A0)
             * 00e6e438 move.w #7,(0x4,A0)
             */
            SMD_GLOBALS.cursor_pending_flag = false;
            SMD_GLOBALS.default_cursor_pos = local_pos;
            SMD_GLOBALS.cursor_button_state = draw_cursor_num;
            cur_hw->cursor_visible = true;
            blink->smd_time_com = (draw_cursor_num == 0) ? true : false;
            blink->blink_flag = true;
            blink->blink_counter = 7;
        } else {
            /* 00e6e440 st (0x1744,A5) */
            SMD_GLOBALS.cursor_pending_flag = true;
        }
    }

    /* 00e6e444 move.w (0x1d98,A5),(0x1d9c,A5) / bsr SMD_$REL_DISPLAY */
    SMD_GLOBALS.previous_unit = SMD_GLOBALS.default_unit;
    SMD_$REL_DISPLAY();
}
