/*
 * smd/sau2/scroll.s - SMD_$START_SCROLL and SMD_$CONTINUE_SCROLL
 *                     (hand-written assembly)
 *
 * Original addresses:
 *   SMD_$START_SCROLL     0x00E272A8 (trampoline, 6 bytes)
 *                         0x00E15C68 (body, 0x34 bytes)
 *   SMD_$CONTINUE_SCROLL  0x00E272B2 (trampoline, 6 bytes)
 *                         0x00E15C9C (body, 0x32 bytes)
 *
 * Not compiler output, for the same reasons as smd/sau2/start_blt.s and
 * lock_display.s (bead source-a2ip): no link/unlk, a bare "move.l A5,-(SP)"
 * thunk, arguments read straight off SP at (0x8,SP) and (0xc,SP) with the
 * pushed register's displacement folded in by hand, a dispatch base handed in
 * through A0 by the trampoline, and a call made by jumping INTO the dispatch
 * block with "jsr (0x150,A5)" rather than through a function pointer.
 *
 * A0 is a REGISTER ARGUMENT: each trampoline's "lea (d16,PC),A0" leaves
 * 0x00E26F20 there - 0x00E272AA - 0x38A and 0x00E272B4 - 0x394 are the same
 * address - which is SMD_$DISP1_INT, the base smd/sau2/disp1_int.s defines.
 * The body copies it into A5 and then calls A5+0x150 == 0x00E27070, i.e.
 * smd_$setup_scroll_blt, which is why that routine can never be reached
 * through an ordinary C prototype.
 *
 * Entry (Pascal calling sequence, caller cleans up):
 *   (0x8,SP)   smd_display_hw_t *hw
 *   (0xc,SP)   volatile uint16_t *ctrl_regs   - the unit record's +0xFC field
 * (displacements are +4 over the usual (0x4,SP) because of the A5 push.)
 *
 * smd_$setup_scroll_blt returns its BLT control word in D0; both routines
 * then OR in hw->video_flags and 0x8010 and store the result to ctrl_regs[0],
 * which is what actually starts the transfer.
 *
 * Transcribed instruction for instruction:
 *   00e272a8    lea (-0x38a,PC),A0            ; -> 0x00e26f20
 *   00e272ac    jmp 0x00e15c68.l
 *   00e15c68    move.l A5,-(SP)
 *   00e15c6a    movea.l A0,A5
 *   00e15c6c    movea.l (0x8,SP),A1           ; A1 = hw
 *   00e15c70    movea.l (0xc,SP),A0           ; A0 = ctrl_regs
 *   00e15c74    move.w #0x2,(0x2,A1)          ; hw->lock_state = 2 (scrolling)
 *   00e15c7a    ori.w #0x20,(0x22,A1)         ; hw->video_flags |= 0x20
 *   00e15c80    clr.w (0x20,A1)               ; the whole word at hw+0x20
 *   00e15c84    move.l (0x10,A1),(0x1c,A1)    ; hw->field_1c = hw->op_ec.value
 *   00e15c8a    jsr (0x150,A5)                ; smd_$setup_scroll_blt
 *   00e15c8e    or.w (0x22,A1),D0w
 *   00e15c92    or.w #-0x7ff0,D0w             ; 0x8010
 *   00e15c96    move.w D0w,(A0)               ; start the BLT
 *   00e15c98    movea.l (SP)+,A5
 *   00e15c9a    rts
 *
 *   00e272b2    lea (-0x394,PC),A0            ; -> 0x00e26f20
 *   00e272b6    jmp 0x00e15c9c.l
 *   00e15c9c    move.l A5,-(SP)
 *   00e15c9e    movea.l A0,A5
 *   00e15ca0    movea.l (0x8,SP),A1
 *   00e15ca4    movea.l (0xc,SP),A0
 *   00e15ca8    tst.w (0x24,A1)               ; anything left to scroll?
 *   00e15cac    bne.b 0x00e15cb8
 *   00e15cae    move.w #0x3,(0x2,A1)          ; hw->lock_state = 3 (done)
 *   00e15cb4    movea.l (SP)+,A5
 *   00e15cb6    rts
 *   00e15cb8    jsr (0x150,A5)
 *   00e15cbc    or.w (0x22,A1),D0w
 *   00e15cc0    or.w #-0x7ff0,D0w
 *   00e15cc4    move.w D0w,(A0)
 *   00e15cc6    move.w #0x2,(0x2,A1)          ; still scrolling
 *   00e15ccc    bra.b 0x00e15cb4
 *
 * Verified with m68k-elf-gcc -c + m68k-elf-objdump -d: both bodies assemble
 * to the image's byte sequences
 *
 *   00e15c68  2f 0d 2a 48 22 6f 00 08 20 6f 00 0c 33 7c 00 02
 *   00e15c78  00 02 00 69 00 20 00 22 42 69 00 20 23 69 00 10
 *   00e15c88  00 1c 4e ad 01 50 80 69 00 22 80 7c 80 10 30 80
 *   00e15c98  2a 5f 4e 75
 *   00e15c9c  2f 0d 2a 48 22 6f 00 08 20 6f 00 0c 4a 69 00 24
 *   00e15cac  66 0a 33 7c 00 03 00 02 2a 5f 4e 75 4e ad 01 50
 *   00e15cbc  80 69 00 22 80 7c 80 10 30 80 33 7c 00 02 00 02
 *   00e15ccc  60 e6
 *
 * except for the relocated bytes in each trampoline (the `lea`'s two-byte
 * PC displacement and the `jmp`'s four-byte absolute address) and the
 * "jsr (0x150,A5)" displacement, which is written literally here because A5
 * is a run-time value rather than a symbol.
 *
 * The portable C models in smd/start_scroll.c and smd/continue_scroll.c are
 * compiled only when ARCH_M68K is not defined, so the two never collide at
 * link time.
 */

        .section ".text.SMD_$START_SCROLL","ax",@progbits
        .globl  SMD_$START_SCROLL
        .globl  SMD_$CONTINUE_SCROLL

SMD_$START_SCROLL:
        lea     (SMD_$DISP1_INT:w,%pc),%a0      /* 00e272a8                  */
        jmp     (Lstart_scroll_body).l          /* 00e272ac                  */

Lstart_scroll_body:
        move.l  %a5,-(%sp)              /* 00e15c68                          */
        movea.l %a0,%a5                 /* 00e15c6a: A5 = the dispatch base  */
        movea.l 0x8(%sp),%a1            /* 00e15c6c: A1 = hw                 */
        movea.l 0xc(%sp),%a0            /* 00e15c70: A0 = ctrl_regs          */
        move.w  #0x2,0x2(%a1)           /* 00e15c74: lock_state = scrolling  */
        ori.w   #0x20,0x22(%a1)         /* 00e15c7a: video_flags |= 0x20     */
        clr.w   0x20(%a1)               /* 00e15c80: the whole word          */
        move.l  0x10(%a1),0x1c(%a1)     /* 00e15c84                          */
        jsr     0x150(%a5)              /* 00e15c8a: smd_$setup_scroll_blt   */
        or.w    0x22(%a1),%d0           /* 00e15c8e                          */
        /*
         * 00e15c92  80 7c 80 10  or.w #-0x7ff0,D0
         * The original uses the OR-with-immediate-source form (0x807C); GNU
         * as normalises "or.w #imm,%d0" to ORI.W (0x0040), same length and
         * same flags but different bytes.  Emitted literally.
         */
        .short  0x807c, 0x8010          /* 00e15c92: or.w #0x8010,%d0        */
        move.w  %d0,(%a0)               /* 00e15c96: start the BLT           */
        movea.l (%sp)+,%a5              /* 00e15c98                          */
        rts                             /* 00e15c9a                          */

        .section ".text.SMD_$CONTINUE_SCROLL","ax",@progbits
        .balign 2
SMD_$CONTINUE_SCROLL:
        lea     (SMD_$DISP1_INT:w,%pc),%a0      /* 00e272b2                  */
        jmp     (Lcontinue_scroll_body).l       /* 00e272b6                  */

Lcontinue_scroll_body:
        move.l  %a5,-(%sp)              /* 00e15c9c                          */
        movea.l %a0,%a5                 /* 00e15c9e                          */
        movea.l 0x8(%sp),%a1            /* 00e15ca0: A1 = hw                 */
        movea.l 0xc(%sp),%a0            /* 00e15ca4: A0 = ctrl_regs          */
        tst.w   0x24(%a1)               /* 00e15ca8: anything left?          */
        bne.b   Lcontinue_step          /* 00e15cac -> 00e15cb8              */
        move.w  #0x3,0x2(%a1)           /* 00e15cae: lock_state = done       */

Lcontinue_done:
        movea.l (%sp)+,%a5              /* 00e15cb4                          */
        rts                             /* 00e15cb6                          */

Lcontinue_step:
        jsr     0x150(%a5)              /* 00e15cb8: smd_$setup_scroll_blt   */
        or.w    0x22(%a1),%d0           /* 00e15cbc                          */
        .short  0x807c, 0x8010          /* 00e15cc0: or.w #0x8010,%d0        */
        move.w  %d0,(%a0)               /* 00e15cc4: start the BLT           */
        move.w  #0x2,0x2(%a1)           /* 00e15cc6: still scrolling         */
        bra.b   Lcontinue_done          /* 00e15ccc -> 00e15cb4              */
