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
static const uint32_t draw_box_lock_data = 0x00E6DFF8;

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
 *   00e6df4a    pea (0xac,PC)                 ; push lock_data
 *   00e6df4e    bsr.w 0x00e6eb42              ; ACQ_DISPLAY(&lock_data)
 *   00e6df52    addq.w #0x4,SP
 *   00e6df54    move.w D0w,(-0x22,A6)         ; control = result
 *   ; Draw top edge: HORIZ_LINE(y1, x1, x2)
 *   00e6df58    move.l (-0x14,A6),-(SP)       ; param7
 *   00e6df5c    pea (-0x22,A6)                ; &control
 *   00e6df60    move.l (-0x18,A6),-(SP)       ; hw_regs
 *   00e6df64    move.l (-0x1c,A6),-(SP)       ; param4
 *   00e6df68    pea (0x2,A2)                  ; &rect->x2
 *   00e6df6c    pea (A2)                      ; &rect->x1
 *   00e6df6e    pea (0x4,A2)                  ; &rect->y1
 *   00e6df72    jsr 0x00e8496a.l              ; HORIZ_LINE
 *   00e6df78    lea (0x1c,SP),SP
 *   ; Draw right edge: VERT_LINE(x2, y1, y2)
 *   ...etc for remaining edges...
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
    control = SMD_$ACQ_DISPLAY((void *)&draw_box_lock_data);

    /* Draw top edge: horizontal line at y1 from x1 to x2 */
    SMD_$HORIZ_LINE(&rect->y1, &rect->x1, &rect->x2,
                    (void *)ctx.field_04, ctx.hw_regs, &control,
                    (void *)ctx.field_08);

    /* Draw right edge: vertical line at x2 from y1 to y2 */
    SMD_$VERT_LINE(&rect->x2, &rect->y1, &rect->y2,
                   (void *)ctx.field_04, ctx.hw_regs, &control);

    /* Draw bottom edge: horizontal line at y2 from x1 to x2 */
    SMD_$HORIZ_LINE(&rect->y2, &rect->x1, &rect->x2,
                    (void *)ctx.field_04, ctx.hw_regs, &control,
                    (void *)ctx.field_08);

    /* Draw left edge: vertical line at x1 from y1 to y2 */
    SMD_$VERT_LINE(&rect->x1, &rect->y1, &rect->y2,
                   (void *)ctx.field_04, ctx.hw_regs, &control);

    /* Release display lock */
    SMD_$REL_DISPLAY();
}
