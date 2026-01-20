/*
 * smd/clear_window.c - SMD_$CLEAR_WINDOW implementation
 *
 * Clears a rectangular window using hardware BLT acceleration.
 * Uses the clear pattern (0x380) instead of the draw pattern.
 *
 * Original address: 0x00E8495C (thunk) -> 0x00E706C0 (actual code)
 */

#include "smd/smd_internal.h"

/* Lock data for display acquisition */
static const uint32_t clear_window_lock_data = 0x00E84958;

/*
 * SMD_$CLEAR_WINDOW - Clear rectangular window
 *
 * Clears the specified rectangular region using hardware-accelerated
 * BLT operations. Uses the clear pattern rather than the line drawing
 * pattern.
 *
 * Parameters:
 *   rect       - Pointer to rectangle structure with x1, x2, y1, y2
 *   status_ret - Status return
 *
 * On success:
 *   The rectangle is cleared, status_ret is status_$ok
 *
 * On failure:
 *   status_ret contains the error code from UTIL_INIT
 *
 * Original address: 0x00E8495C
 *
 * Assembly (main routine at 0x00E706C0):
 *   00e706c0    movem.l {  A5 A3 A2 D7 D6 D5},-(SP)
 *   00e706c4    movea.l A0,A5                   ; A5 = thunk data pointer
 *   00e706c6    link.w A6,-0x1c
 *   00e706ca    pea (-0x1c,A6)                  ; push &ctx
 *   00e706ce    movea.l (0x32,A5),A0            ; A0 = UTIL_INIT ptr
 *   00e706d2    jsr (A0)                        ; call UTIL_INIT
 *   00e706d4    addq.w #0x4,SP
 *   00e706d6    movea.l (0x24,A6),A0            ; A0 = status_ret
 *   00e706da    move.l (-0xc,A6),(A0)           ; *status_ret = ctx.status
 *   00e706de    bne.w 0x00e70758                ; if error, exit
 *   00e706e2    movea.l (0x20,A6),A0            ; A0 = rect
 *   00e706e6    move.l (A0)+,D6                 ; D6 = packed x coords
 *   00e706e8    move.l (A0),D7                  ; D7 = packed y coords
 *   00e706ea    movea.l (-0x18,A6),A2           ; A2 = ctx.field_04
 *   00e706ee    movea.l (-0x14,A6),A3           ; A3 = ctx.hw_regs
 *   00e706f2    pea (0x24,A5)                   ; push lock data
 *   00e706f6    movea.l (0x18,A5),A0            ; A0 = ACQ_DISPLAY ptr
 *   00e706fa    jsr (A0)                        ; call ACQ_DISPLAY
 *   00e706fc    addq.w #0x4,SP
 *   00e706fe    move.w D0w,D5w                  ; D5 = control value
 *   00e70700    move.w #0x380,(0x6,A3)          ; pattern = 0x380 (clear)
 *   00e70706    move.w #0x3ff,(0x4,A3)          ; mask = 0x3FF
 *   00e7070c    move.w D6w,D0w                  ; D0 = low word (x2)
 *   00e7070e    and.w #0xf,D0w                  ; D0 = x2 & 0xF
 *   00e70712    move.w D0w,(0x2,A3)             ; bit_pos = x2 & 0xF
 *   00e70716    swap D6                         ; swap to get x1
 *   00e70718    move.w D6w,(0xe,A3)             ; x_start = x1
 *   00e7071c    move.w D6w,D0w                  ; D0 = x1
 *   00e7071e    lsr.w #0x4,D0w                  ; D0 = x1 >> 4
 *   00e70720    swap D6                         ; back to x2
 *   00e70722    lsr.w #0x4,D6w                  ; D6 = x2 >> 4
 *   00e70724    sub.w D0w,D6w                   ; D6 = (x2>>4) - (x1>>4)
 *   00e70726    blt.b 0x00e7072a                ; if negative, skip
 *   00e70728    neg.w D6w                       ; negate
 *   00e7072a    subq.w #0x1,D6w                 ; D6 = width - 1
 *   00e7072c    move.w D6w,(0xa,A3)             ; x_extent = width - 1
 *   00e70730    move.w D7w,D0w                  ; D0 = y2
 *   00e70732    swap D7                         ; swap to get y1
 *   00e70734    move.w D7w,(0xc,A3)             ; y_start = y1
 *   00e70738    sub.w D7w,D0w                   ; D0 = y2 - y1
 *   00e7073a    blt.b 0x00e7073e                ; if negative, skip
 *   00e7073c    neg.w D0w                       ; negate
 *   00e7073e    subq.w #0x1,D0w                 ; D0 = height - 1
 *   00e70740    move.w D0w,(0x8,A3)             ; y_extent = height - 1
 *   00e70744    or.w #-0x7ff2,D5w               ; D5 |= 0x800E
 *   00e70748    movea.l (-0x10,A6),A0           ; (unused)
 *   00e7074c    move.w D5w,(A3)                 ; start operation
 *   00e7074e    tst.w (A3)                      ; test control
 *   00e70750    bmi.b 0x00e7074e                ; loop while busy
 *   00e70752    movea.l (0x1c,A5),A0            ; A0 = REL_DISPLAY ptr
 *   00e70756    jsr (A0)                        ; call REL_DISPLAY
 *   00e70758    unlk A6
 */
void SMD_$CLEAR_WINDOW(smd_rect_t *rect, status_$t *status_ret)
{
    smd_util_ctx_t ctx;
    uint16_t control;
    uint16_t x1, x2, y1, y2;
    int16_t width, height;

    /* Initialize utility context */
    SMD_$UTIL_INIT(&ctx);
    *status_ret = ctx.status;

    if (ctx.status != status_$ok) {
        return;
    }

    /* Extract coordinates */
    x1 = (uint16_t)rect->x1;
    x2 = (uint16_t)rect->x2;
    y1 = (uint16_t)rect->y1;
    y2 = (uint16_t)rect->y2;

    /* Acquire display for exclusive access */
    control = SMD_$ACQ_DISPLAY((void *)&clear_window_lock_data);

    /* Set pattern for clearing */
    ctx.hw_regs->pattern = SMD_BLT_PATTERN_CLEAR;

    /* Set default mask */
    ctx.hw_regs->mask = SMD_BLT_DEFAULT_MASK;

    /* Set bit position and X start */
    ctx.hw_regs->bit_pos = x2 & 0x0F;
    ctx.hw_regs->x_start = x1;

    /* Calculate width in words */
    width = (int16_t)((x2 >> 4) - (x1 >> 4));
    if (width >= 0) {
        width = -width;
    }
    ctx.hw_regs->x_extent = (uint16_t)(width - 1);

    /* Set Y start */
    ctx.hw_regs->y_start = y1;

    /* Calculate height */
    height = (int16_t)(y2 - y1);
    if (height >= 0) {
        height = -height;
    }
    ctx.hw_regs->y_extent = (uint16_t)(height - 1);

    /* Start the BLT operation */
    ctx.hw_regs->control = control | SMD_BLT_CMD_START_DRAW;

    /* Busy-wait for completion */
    while ((int16_t)ctx.hw_regs->control < 0) {
        /* Spin until bit 15 clears */
    }

    /* Release display lock */
    SMD_$REL_DISPLAY();
}
