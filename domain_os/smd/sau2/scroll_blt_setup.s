/*
 * smd/sau2/scroll_blt_setup.s - SAU2 scroll BLT setup routine
 *
 * Sets up BLT registers for hardware scroll operations.
 * This is the SAU2-specific implementation of the scroll BLT parameter setup.
 *
 * Original address: 0x00E27070
 *
 * This function is called via the SAU dispatch table at offset 0x150.
 * It sets up BLT parameters based on the scroll direction and amount.
 *
 * Entry:
 *   A0 - Pointer to BLT register block
 *   A1 - Pointer to display hardware structure (smd_display_hw_t)
 *
 * Exit:
 *   D0.w - BLT control flags (0x04 for up/down, 0x0C for left/right)
 *
 * BLT Register Block Layout (at A0):
 *   +0x00: Control/status (bit 15 = busy)
 *   +0x02: Bit position within word (x & 0xF)
 *   +0x04: Y destination coordinate
 *   +0x06: X start position
 *   +0x08: Height extent - 1
 *   +0x0A: Width extent in 16-pixel units - 1
 *   +0x0C: Y source coordinate
 *   +0x0E: X source/destination end position
 *
 * smd_display_hw_t offsets used:
 *   +0x10: op_ec (operation event count)
 *   +0x24: field_24 (remaining scroll flag)
 *   +0x26: scroll_x1 (left x coordinate)
 *   +0x28: scroll_y1 (top y coordinate - actually encoded as y|x in 16px units)
 *   +0x2A: scroll_x2 (right x coordinate)
 *   +0x2C: scroll_y2 (bottom y coordinate)
 *   +0x2E: scroll_dy (remaining scroll amount)
 *   +0x30: scroll_dx (scroll direction: 0=down, 1=up, 2=right, 3=left)
 *
 * Scroll directions:
 *   0 = Scroll down (content moves up) - y destination < y source
 *   1 = Scroll up (content moves down) - y destination > y source
 *   2 = Scroll right (content moves left) - x destination > x source
 *   3 = Scroll left (content moves right) - x destination < x source
 */

        .text
        .globl  smd_$setup_scroll_blt
        .globl  _smd_$setup_scroll_blt

smd_$setup_scroll_blt:
_smd_$setup_scroll_blt:
        /* Check if BLT is currently busy */
        tst.w   (%a0)                   /* Test BLT control register */
        bmi.w   blt_in_use_error        /* If bit 15 set (busy), error */

        /* Decrement scroll_dy by 2 (process 2 pixels per iteration) */
        subq.w  #2, 0x2e(%a1)           /* scroll_dy -= 2 */
        blt.b   scroll_step_done        /* If negative, this is last step */

        /* More scrolling to do */
        moveq   #2, %d1                 /* D1 = 2 (step amount for calculations) */
        bra.b   check_advance_ec

scroll_step_done:
        /* Last scroll step - clear the remaining flag */
        moveq   #1, %d1                 /* D1 = 1 (final step adjustment) */
        clr.w   0x24(%a1)               /* field_24 = 0 (scroll complete flag) */

check_advance_ec:
        /* Check if scroll_dy <= 0, advance event count if so */
        tst.w   0x2e(%a1)               /* Test scroll_dy */
        bgt.b   setup_blt_params        /* If > 0, continue with setup */

        /* Advance operation event count to signal completion */
        movem.l %d0-%d1/%a0-%a1, -(%sp) /* Save registers */
        pea     0x10(%a1)               /* Push &hw->op_ec */
        jsr     EC_$ADVANCE             /* Advance event count */
        addq.w  #4, %sp                 /* Clean up stack */
        movem.l (%sp)+, %d0-%d1/%a0-%a1 /* Restore registers */

setup_blt_params:
        /* Get scroll direction and dispatch */
        move.w  0x30(%a1), %d0          /* D0 = scroll_dx (direction) */
        movem.l %d2-%d3, -(%sp)         /* Save work registers */

        cmp.w   #0, %d0
        beq.b   scroll_down             /* Direction 0: scroll down */
        cmp.w   #1, %d0
        beq.b   scroll_up               /* Direction 1: scroll up */
        cmp.w   #2, %d0
        beq.w   scroll_right            /* Direction 2: scroll right */
        cmp.w   #3, %d0
        beq.w   scroll_left             /* Direction 3: scroll left */

        /* Invalid direction - crash */
        pea     SMD_Invalid_Direction_From_SM_Err
        jsr     CRASH_SYSTEM

/*
 * scroll_down - Scroll content down (viewport moves up)
 *
 * Copies pixels from y+step to y, moving content upward.
 * BLT source is below destination.
 */
scroll_down:
        move.w  0x26(%a1), %d0          /* D0 = scroll_x1 */
        move.w  %d0, 0x06(%a0)          /* blt->x_start = scroll_x1 */
        move.w  %d0, 0x0e(%a0)          /* blt->x_end = scroll_x1 */

        move.w  0x28(%a1), %d2          /* D2 = scroll_y1 (packed y|x16) */
        move.w  %d2, %d3
        and.w   #0xf, %d3               /* D3 = x bit position (low 4 bits) */
        move.w  %d3, 0x02(%a0)          /* blt->bit_pos = x & 0xF */

        lsr.w   #4, %d0                 /* D0 = scroll_x1 / 16 */
        lsr.w   #4, %d2                 /* D2 = scroll_y1 / 16 */
        sub.w   %d0, %d2                /* D2 = (scroll_y1 - scroll_x1) / 16 */
        blt.b   1f
        neg.w   %d2
1:      subq.w  #1, %d2
        move.w  %d2, 0x0a(%a0)          /* blt->width = |scroll_y1/16 - scroll_x1/16| - 1 */

        move.w  0x2a(%a1), %d0          /* D0 = scroll_x2 */
        move.w  %d0, 0x0c(%a0)          /* blt->y_src = scroll_x2 */
        add.w   %d1, %d0                /* D0 = scroll_x2 + step */
        move.w  %d0, 0x04(%a0)          /* blt->y_dst = scroll_x2 + step */

        sub.w   0x2c(%a1), %d0          /* D0 = y_dst - scroll_y2 */
        blt.b   2f
        neg.w   %d0
2:      subq.w  #1, %d0
        move.w  %d0, 0x08(%a0)          /* blt->height = |y_dst - scroll_y2| - 1 */

        move.w  #0x0c, %d0              /* Return control flags */
        movem.l (%sp)+, %d2-%d3
        rts

/*
 * scroll_up - Scroll content up (viewport moves down)
 *
 * Copies pixels from y to y+step, moving content downward.
 * BLT source is above destination.
 */
scroll_up:
        move.w  0x26(%a1), %d0          /* D0 = scroll_x1 */
        move.w  %d0, 0x06(%a0)          /* blt->x_start = scroll_x1 */
        move.w  %d0, 0x0e(%a0)          /* blt->x_end = scroll_x1 */

        move.w  0x28(%a1), %d2          /* D2 = scroll_y1 */
        move.w  %d2, %d3
        and.w   #0xf, %d3
        move.w  %d3, 0x02(%a0)          /* blt->bit_pos */

        lsr.w   #4, %d0
        lsr.w   #4, %d2
        sub.w   %d0, %d2
        blt.b   1f
        neg.w   %d2
1:      subq.w  #1, %d2
        move.w  %d2, 0x0a(%a0)          /* blt->width */

        move.w  0x2c(%a1), %d0          /* D0 = scroll_y2 */
        move.w  %d0, 0x0c(%a0)          /* blt->y_src = scroll_y2 */
        sub.w   %d1, %d0                /* D0 = scroll_y2 - step */
        move.w  %d0, 0x04(%a0)          /* blt->y_dst = scroll_y2 - step */

        sub.w   0x2a(%a1), %d0          /* D0 = y_dst - scroll_x2 */
        blt.b   2f
        neg.w   %d0
2:      subq.w  #1, %d0
        move.w  %d0, 0x08(%a0)          /* blt->height */

        move.w  #0x04, %d0              /* Return control flags */
        movem.l (%sp)+, %d2-%d3
        rts

/*
 * scroll_right - Scroll content right (viewport moves left)
 *
 * Copies pixels from x to x+step, moving content rightward.
 */
scroll_right:
        move.w  0x26(%a1), %d0          /* D0 = scroll_x1 */
        move.w  %d0, 0x0e(%a0)          /* blt->x_end = scroll_x1 */
        move.w  %d0, %d2
        add.w   %d1, %d0                /* D0 = scroll_x1 + step */
        move.w  %d0, 0x06(%a0)          /* blt->x_start = scroll_x1 + step */

        lsr.w   #4, %d2                 /* D2 = scroll_x1 / 16 */
        move.w  0x28(%a1), %d0          /* D0 = scroll_y1 */
        sub.w   %d1, %d0                /* D0 = scroll_y1 - step */
        move.w  %d0, %d3
        and.w   #0xf, %d3
        move.w  %d3, 0x02(%a0)          /* blt->bit_pos */

        lsr.w   #4, %d0
        sub.w   %d2, %d0
        blt.b   1f
        neg.w   %d0
1:      subq.w  #1, %d0
        move.w  %d0, 0x0a(%a0)          /* blt->width */

        move.w  0x2a(%a1), %d0          /* D0 = scroll_x2 */
        move.w  %d0, 0x0c(%a0)          /* blt->y_src = scroll_x2 */
        move.w  %d0, 0x04(%a0)          /* blt->y_dst = scroll_x2 */
        bra.b   calc_height_horiz

/*
 * scroll_left - Scroll content left (viewport moves right)
 *
 * Copies pixels from x+step to x, moving content leftward.
 */
scroll_left:
        move.w  0x26(%a1), %d0          /* D0 = scroll_x1 */
        move.w  %d0, 0x06(%a0)          /* blt->x_start = scroll_x1 */
        add.w   %d1, %d0                /* D0 = scroll_x1 + step */
        move.w  %d0, 0x0e(%a0)          /* blt->x_end = scroll_x1 + step */

        lsr.w   #4, %d0                 /* D0 = (scroll_x1 + step) / 16 */
        move.w  0x28(%a1), %d2          /* D2 = scroll_y1 */
        move.w  %d2, %d3
        and.w   #0xf, %d3
        move.w  %d3, 0x02(%a0)          /* blt->bit_pos */

        lsr.w   #4, %d2
        sub.w   %d0, %d2
        blt.b   1f
        neg.w   %d2
1:      subq.l  #1, %d2
        move.w  %d2, 0x0a(%a0)          /* blt->width */

calc_height_horiz:
        /* Common height calculation for horizontal scrolls */
        move.w  0x2a(%a1), %d0          /* D0 = scroll_x2 */
        move.w  %d0, 0x0c(%a0)          /* blt->y_src = scroll_x2 */
        move.w  %d0, 0x04(%a0)          /* blt->y_dst = scroll_x2 */

        sub.w   0x2c(%a1), %d0          /* D0 = scroll_x2 - scroll_y2 */
        blt.b   2f
        neg.w   %d0
2:      subq.w  #1, %d0
        move.w  %d0, 0x08(%a0)          /* blt->height */

        move.w  #0x0c, %d0              /* Return control flags */
        movem.l (%sp)+, %d2-%d3
        rts

/*
 * blt_in_use_error - BLT engine is busy, crash
 */
blt_in_use_error:
        pea     SMD_Invalid_BLT_In_Use_Err
        jsr     CRASH_SYSTEM
        addq.w  #4, %sp
        bra.b   blt_in_use_error        /* Loop forever (CRASH_SYSTEM doesn't return) */

        .data

SMD_Invalid_BLT_In_Use_Err:
        .word   0x0007                  /* Error module: SMD (0x13>>1 = 7?) */
        .word   0x0013                  /* Subsystem: SMD */

SMD_Invalid_Direction_From_SM_Err:
        .word   0x0008                  /* Error code within module */
        .word   0x0013                  /* Subsystem: SMD */
