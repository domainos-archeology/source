/*
 * smd/draw_box.c - SMD_$DRAW_BOX implementation
 *
 * Draws a rectangular box outline using hardware BLT acceleration.
 * Draws four lines: top, right, bottom, and left edges.
 *
 * Original address: 0x00E6DF2A
 */

#include "smd/smd_internal.h"

/* Lock data for display acquisition - defined in original code */

/*
 * SMD_$DRAW_BOX - Draw rectangular box outline
 *
 * Draws the four edges of a rectangle using hardware-accelerated
 * line drawing. Acquires the display lock for exclusive access
 * during the operation.
 *
 * Parameters:
 *   rect       - Pointer to rectangle structure with x1, x2, y1, y2
 *   status_ret - Status return
 *
 * On success:
 *   All four edges are drawn, status_ret is status_$ok
 *
 * On failure:
 *   status_ret contains the error code from UTIL_INIT
 *
 * Original address: 0x00E6DF2A
 *
 * Assembly:
 *   00e6df2a    link.w A6,-0x24
 *   00e6df2e    movem.l {  A3 A2},-(SP)
 *   00e6df32    movea.l (0x8,A6),A2            ; A2 = rect
 *   00e6df36    movea.l (0xc,A6),A3            ; A3 = status_ret
 *   00e6df3a    pea (-0x20,A6)                 ; push &ctx
 *   00e6df3e    bsr.b 0x00e6ded4              ; UTIL_INIT(&ctx)
 *   00e6df40    addq.w #0x4,SP
 *   00e6df42    move.l (-0x10,A6),(A3)        ; *status_ret = ctx.status
 *   00e6df46    bne.w 0x00e6dfee              ; if error, exit
 *   00e6df4a    pea (0xac,PC)                 ; &SMD_SYNC_LOCK_DATA
 *                                          ; (0x00e6df4c + 0xac = 0x00e6dff8)
 *   00e6df4e    bsr.w 0x00e6eb42              ; ACQ_DISPLAY(&lock_data)
 *   00e6df52    addq.w #0x4,SP
 *   00e6df54    move.w D0w,(-0x22,A6)         ; control = result
 *   ; the utility context is the 0x14 bytes at A6-0x20, so
 *   ;   ctx.display_base = (-0x1c,A6), ctx.ctrl_regs = (-0x18,A6),
 *   ;   ctx.hw           = (-0x14,A6), ctx.status    = (-0x10,A6)
 *   ; Draw top edge: HORIZ_LINE(&y1, &x1, &x2, base, regs, &ctl, hw)
 *   00e6df58    move.l (-0x14,A6),-(SP)       ; arg7 = ctx.hw
 *   00e6df5c    pea (-0x22,A6)                ; arg6 = &control
 *   00e6df60    move.l (-0x18,A6),-(SP)       ; arg5 = ctx.ctrl_regs
 *   00e6df64    move.l (-0x1c,A6),-(SP)       ; arg4 = ctx.display_base
 *   00e6df68    pea (0x2,A2)                  ; arg3 = &rect->x2
 *   00e6df6c    pea (A2)                      ; arg2 = &rect->x1
 *   00e6df6e    pea (0x4,A2)                  ; arg1 = &rect->y1
 *   00e6df72    jsr 0x00e8496a.l              ; HORIZ_LINE
 *   00e6df78    lea (0x1c,SP),SP              ; 7 longwords
 *   ; Draw right edge: VERT_LINE(&x2, &y1, &y2, base, regs, &ctl, hw)
 *   00e6df7c    move.l (-0x14,A6),-(SP)       ; arg7 = ctx.hw
 *   00e6df80    pea (-0x22,A6)                ; arg6 = &control
 *   00e6df84    move.l (-0x18,A6),-(SP)       ; arg5 = ctx.ctrl_regs
 *   00e6df88    move.l (-0x1c,A6),-(SP)       ; arg4 = ctx.display_base
 *   00e6df8c    pea (0x6,A2)                  ; arg3 = &rect->y2
 *   00e6df90    pea (0x4,A2)                  ; arg2 = &rect->y1
 *   00e6df94    pea (0x2,A2)                  ; arg1 = &rect->x2
 *   00e6df98    jsr 0x00e84974.l              ; VERT_LINE (also 7 arguments)
 *   00e6df9e    lea (0x1c,SP),SP
 *   ; Draw bottom edge: HORIZ_LINE(&y2, &x1, &x2, ...) - 00e6dfa2-00e6dfc2
 *   ; Draw left edge:   VERT_LINE(&x1, &y1, &y2, ...) - 00e6dfc6-00e6dfe6
 *   00e6dfea    bsr.w 0x00e6ec10              ; REL_DISPLAY
 *   00e6dfee    movem.l (-0x2c,A6),{  A2 A3}
 *   00e6dff4    unlk A6
 *   00e6dff6    rts
 */
void SMD_$DRAW_BOX(smd_rect_t *rect, status_$t *status_ret)
{
    smd_util_ctx_t ctx;
    uint16_t control;

    /* Initialize utility context */
    SMD_$UTIL_INIT(&ctx);
    *status_ret = ctx.status;

    if (ctx.status != status_$ok) {
        return;
    }

    /* Acquire display for exclusive access */
    control = SMD_$ACQ_DISPLAY(&SMD_SYNC_LOCK_DATA);

    /*
     * Every call takes seven arguments; the fifth is the controller register
     * block (ctx+0x08) and the seventh the hardware info record (ctx+0x0C).
     * See the pushes traced above - the C used to have those two swapped and
     * to drop VERT_LINE's seventh argument entirely.
     */

    /* Draw top edge: horizontal line at y1 from x1 to x2 (0x00e6df58) */
    SMD_$HORIZ_LINE(&rect->y1, &rect->x1, &rect->x2,
                    (void *)(uintptr_t)ctx.display_base, ctx.ctrl_regs,
                    &control, (void *)ctx.hw);

    /* Draw right edge: vertical line at x2 from y1 to y2 (0x00e6df7c) */
    SMD_$VERT_LINE(&rect->x2, &rect->y1, &rect->y2,
                   (void *)(uintptr_t)ctx.display_base, ctx.ctrl_regs,
                   &control, (void *)ctx.hw);

    /* Draw bottom edge: horizontal line at y2 from x1 to x2 (0x00e6dfa2) */
    SMD_$HORIZ_LINE(&rect->y2, &rect->x1, &rect->x2,
                    (void *)(uintptr_t)ctx.display_base, ctx.ctrl_regs,
                    &control, (void *)ctx.hw);

    /* Draw left edge: vertical line at x1 from y1 to y2 (0x00e6dfc6) */
    SMD_$VERT_LINE(&rect->x1, &rect->y1, &rect->y2,
                   (void *)(uintptr_t)ctx.display_base, ctx.ctrl_regs,
                   &control, (void *)ctx.hw);

    /* Release display lock */
    SMD_$REL_DISPLAY();
}
