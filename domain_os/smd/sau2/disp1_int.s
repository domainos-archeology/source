/*
 * smd/sau2/disp1_int.s - SMD_$DISP1_INT (hand-written assembly)
 *
 * Display-1 BLT/scroll interrupt handler.
 *
 * Original address: 0x00E26F20, 0x106 bytes (0x00E26F20..0x00E27025), followed
 * immediately by the four status constants this module passes to CRASH_SYSTEM
 * (0x00E27026..0x00E27035) and by its scroll-BLT setup subroutine at
 * 0x00E27036.
 *
 * Not compiler output: no link/unlk, an interrupt prologue that raises the IPL
 * with `ori #0x600,SR`, a hand-built `jmp (d8,PC,Dn.w)` jump table, arguments
 * to the setup subroutine passed in A0/A1 with the result in D0, and two tail
 * `jmp`s into PROC1's interrupt-exit paths instead of an `rts`.  Ghidra never
 * created a function here (`gsk analyze 0x00E26F20` reports "No function
 * found"); the listing below was produced with m68k-elf-objdump over the image
 * bytes from `gsk read 0x00E26F20 0x106`.
 *
 * Entry: a hardware interrupt vector; nothing is passed in registers.
 *   A1 = 0x00E27376 = SMD_$DISPLAY_COM = &SMD_DISPLAY_INFO[0], unit 1's
 *        hardware record (smd_display_hw_t)
 *   A0 = 0x00FF9800, the SAU2 display controller register block
 *
 * smd_display_hw_t fields it touches:
 *   +0x02 lock_state   the BLT state machine's state, 0..7 (jump table index)
 *   +0x10 op_ec        advanced when an operation completes
 *   +0x20 (byte)       "another step is pending" flag
 *   +0x22 video_flags  the word written back to the controller
 *   +0x24 field_24     bumped for the two states that ignore the interrupt
 *   +0x2E scroll_dy    steps left in a multi-step scroll
 *
 * Controller registers (A0):
 *   (A0)      control/status; bit 15 = BLT busy
 *   (0x1,A0)  status byte; negative = "BLT done" interrupt
 *   (0x2,A0)  read once to acknowledge the interrupt, result discarded
 */

        .text
        .globl  SMD_$DISP1_INT
        .globl  SMD_Invalid_Direction_From_SM_Err
        .globl  SMD_Invalid_BLT_In_Use_Err
        .globl  SMD_Invalid_BLT_Done_Interrupt_Err
        .globl  SMD_Invalid_Interrupt_Routine_State_Err

SMD_$DISP1_INT:
        movem.l %d0-%d1/%a0-%a1,-(%sp)  /* 00e26f20  48e7 c0c0               */
        ori.w   #0x600,%sr              /* 00e26f24  007c 0600: IPL -> 6     */
        jsr     (IO_$USE_INT_STACK).l   /* 00e26f28  4eb9 00e2 e826          */
        /*
         * 00e26f2e  43fa 0446  lea (0x446,PC),A1
         * 0x00e26f30 + 0x446 = 0x00E27376 = SMD_$DISPLAY_COM.  The symbol
         * lives in smd/smd_data.c, so the assembler emits a relocation here
         * and the two displacement bytes cannot match the image.
         */
        lea     (SMD_DISPLAY_INFO:w,%pc),%a1
        lea     (0xff9800).l,%a0        /* 00e26f32  41f9 00ff 9800          */

        /* 00e26f38 tst.b (0x1,A0) / bge.w: not a BLT-done interrupt */
        tst.b   0x1(%a0)                /* 00e26f38  4a28 0001               */
        bge.w   Lnot_blt_done           /* 00e26f3c  6c00 006e -> 00e26fac   */

        /* 00e26f40: read (0x2,A0) to acknowledge; the value is discarded */
        tst.w   0x2(%a0)                /* 00e26f40  4a68 0002               */

        /* 00e26f44-00e26f4a: dispatch on hw->lock_state * 4 */
        move.w  0x2(%a1),%d0            /* 00e26f44  3029 0002               */
        lsl.w   #2,%d0                  /* 00e26f48  e548                    */
        jmp     (Ltable:b,%pc,%d0.w)    /* 00e26f4a  4efb 0002               */

/*
 * The jump table: eight 4-byte `bra.w` slots for lock_state 0..7.
 * States 0, 1 and 5 clear the "operation running" bit; 2 and 4 only count the
 * interrupt; 3 starts the next scroll step; 6 crashes; 7 exits.
 */
Ltable:
        bra.w   Lclear_run_bit          /* 00e26f4e  6000 0024  state 0      */
        bra.w   Lclear_run_bit          /* 00e26f52  6000 0020  state 1      */
        bra.w   Lcount_only             /* 00e26f56  6000 004e  state 2      */
        bra.w   Lnext_step              /* 00e26f5a  6000 002c  state 3      */
        bra.w   Lcount_only             /* 00e26f5e  6000 0046  state 4      */
        bra.w   Lclear_run_bit          /* 00e26f62  6000 0010  state 5      */
        bra.w   Lbad_state              /* 00e26f66  6000 0064  state 6      */
        bra.w   Lint_advance            /* 00e26f6a  6000 0084  state 7      */

Lint_exit:
        jmp     (PROC1_$INT_EXIT).l     /* 00e26f6e  4ef9 00e2 08fe          */

/*
 * States 0, 1 and 5: drop bit 5 of video_flags and, if the controller is no
 * longer busy, write the new flags back.
 */
Lclear_run_bit:
        move.w  0x22(%a1),%d0           /* 00e26f74  3029 0022               */
        /*
         * 00e26f78  c07c ffdf  and.w #-0x21,D0
         * The original uses AND.W-with-immediate-source (0xC07C); GNU as
         * normalises "and.w #imm,%d0" to ANDI.W (0x0240), same length and
         * same flags but different bytes.  Emitted literally.
         */
        .short  0xc07c, 0xffdf          /* 00e26f78: and.w #0xffdf,%d0       */
        move.w  %d0,0x22(%a1)           /* 00e26f7c  3340 0022               */
        tst.w   (%a0)                   /* 00e26f80  4a50                    */
        bmi.b   Lint_exit               /* 00e26f82  6bea: still busy        */
        move.w  %d0,(%a0)               /* 00e26f84  3080                    */
        bra.b   Lint_exit               /* 00e26f86  60e6                    */

/*
 * State 3: build the next scroll BLT and start it, then move to state 2.
 * The subroutine takes A0 (controller registers) and A1 (hardware record) and
 * returns the BLT control bits in D0.
 */
Lnext_step:
        movem.l %d1-%d3,-(%sp)          /* 00e26f88  48e7 7000               */
        jsr     (smd_$disp1_setup_blt:w,%pc) /* 00e26f8c  4eba 00a8          */
        movem.l (%sp)+,%d1-%d3          /* 00e26f90  4cdf 000e               */
        /*
         * 00e26f94  807c 8010  or.w #-0x7ff0,D0
         * OR.W-with-immediate-source (0x807C) rather than ORI.W (0x0040).
         */
        .short  0x807c, 0x8010          /* 00e26f94: or.w #0x8010,%d0        */
        or.w    0x22(%a1),%d0           /* 00e26f98  8069 0022               */
        move.w  %d0,(%a0)               /* 00e26f9c  3080: start the BLT     */
        move.w  #0x2,0x2(%a1)           /* 00e26f9e  337c 0002 0002          */
        bra.b   Lint_exit               /* 00e26fa4  60c8                    */

/* States 2 and 4: just count the interrupt. */
Lcount_only:
        addq.w  #1,0x24(%a1)            /* 00e26fa6  5269 0024               */
        bra.b   Lint_exit               /* 00e26faa  60c2                    */

/*
 * Not a BLT-done interrupt.  If the controller is nevertheless busy the
 * interrupt is spurious and the system crashes.
 */
Lnot_blt_done:
        tst.w   (%a0)                   /* 00e26fac  4a50                    */
        bge.b   Lcheck_state            /* 00e26fae  6c0a                    */
        pea     (SMD_Invalid_BLT_Done_Interrupt_Err:w,%pc)
                                        /* 00e26fb0  487a 007c              */
        jsr     (CRASH_SYSTEM).l        /* 00e26fb4  4eb9 00e1 e700          */
        /* CRASH_SYSTEM does not return; the original does not clean the
         * argument off the stack and falls straight into Lcheck_state. */

Lcheck_state:
        move.w  0x2(%a1),%d0            /* 00e26fba  3029 0002               */
        .short  0xb07c, 0x0002          /* 00e26fbe: cmp.w #0x2,%d0 (CMP.W,
                                         * not the CMPI.W gas would pick)    */
        beq.w   Lstate2                 /* 00e26fc2  6700 0036 -> 00e26ffa   */
        .short  0xb07c, 0x0001          /* 00e26fc6: cmp.w #0x1,%d0          */
        beq.b   Lstate1                 /* 00e26fca  670a -> 00e26fd6        */

/* Any other state here is a bug; also the state-6 table entry. */
Lbad_state:
        pea     (SMD_Invalid_Interrupt_Routine_State_Err:w,%pc)
                                        /* 00e26fcc  487a 0064              */
        jsr     (CRASH_SYSTEM).l        /* 00e26fd0  4eb9 00e1 e700          */

/* State 1: the operation finished.  Restore the flags, idle, advance op_ec. */
Lstate1:
        move.w  0x22(%a1),(%a0)         /* 00e26fd6  30a9 0022               */
        move.w  #0x0,0x2(%a1)           /* 00e26fda  337c 0000 0002          */
        move.l  %a1,-(%sp)              /* 00e26fe0  2f09: save the record   */
        pea     0x10(%a1)               /* 00e26fe2  4869 0010: &hw->op_ec   */
        /*
         * 00e26fe6  4eb9 00e2 0728  jsr 0x00e20728.l
         * 0x00E20728 is a four-byte gate - "movea.l (0x4,SP),A0" - that falls
         * straight through into ADVANCE_INT at 0x00E2072C.  ec/advance_int.c
         * models the gate and the body as one C function taking the
         * eventcount by reference, so that is the symbol to call.
         */
        jsr     (ADVANCE).l         /* 00e26fe6                          */
        addq.w  #4,%sp                  /* 00e26fec  584f                    */
        movea.l (%sp)+,%a1              /* 00e26fee  225f                    */

/* Also the state-7 table entry. */
Lint_advance:
        lea     0x4(%a1),%a0            /* 00e26ff0  41e9 0004: &hw->lock_ec */
        jmp     (PROC1_$INT_ADVANCE).l  /* 00e26ff4  4ef9 00e2 08f6          */

/* State 2: a scroll step finished. */
Lstate2:
        tst.w   0x2e(%a1)               /* 00e26ffa  4a69 002e: scroll_dy    */
        bgt.b   Lstate2_more            /* 00e26ffe  6e12                    */
        /* No steps left: drop bit 5, write the flags back, go idle. */
        andi.w  #0xffdf,0x22(%a1)       /* 00e27000  0269 ffdf 0022          */
        move.w  0x22(%a1),(%a0)         /* 00e27006  30a9 0022               */
        move.w  #0x0,0x2(%a1)           /* 00e2700a  337c 0000 0002          */
        bra.b   Lint_advance            /* 00e27010  60de                    */

Lstate2_more:
        move.w  0x22(%a1),(%a0)         /* 00e27012  30a9 0022               */
        move.w  #0x3,0x2(%a1)           /* 00e27016  337c 0003 0002          */
        tst.b   0x20(%a1)               /* 00e2701c  4a29 0020               */
        bge.w   Lint_exit               /* 00e27020  6c00 ff4c               */
        bra.b   Lint_advance            /* 00e27024  60ca                    */

/*
 * The four CRASH_SYSTEM status constants that live in this module's code
 * region, in image order.  They are `pea (d16,PC)` targets from here and from
 * the scroll-BLT setup subroutine below, so they must stay at these offsets.
 *
 *   00e27026  00 13 00 07
 *   00e2702a  00 13 00 08
 *   00e2702e  00 13 00 1c
 *   00e27032  00 13 00 1d
 */
SMD_Invalid_Direction_From_SM_Err:
        .long   0x00130007              /* 00e27026                          */
SMD_Invalid_BLT_In_Use_Err:
        .long   0x00130008              /* 00e2702a                          */
SMD_Invalid_BLT_Done_Interrupt_Err:
        .long   0x0013001c              /* 00e2702e                          */
SMD_Invalid_Interrupt_Routine_State_Err:
        .long   0x0013001d              /* 00e27032                          */

/*
 * smd_$disp1_setup_blt - 0x00E27036 - and its twin smd_$setup_scroll_blt at
 * 0x00E27070.  They are TWO ENTRY POINTS OF ONE ROUTINE: each does its own
 * step accounting and eventcount advance and then falls into the shared
 * direction switch at 0x00E270A0, and both share the crash path at
 * 0x00E271FC.
 *
 * The only difference between the entries is how the eventcount is advanced:
 *   0x00E27036 raises IPL first (`move SR,-(SP)` / `ori #0x700,SR` /
 *              `move (SP)+,SR`) and calls 0x00E20728 - the four-byte gate
 *              `movea.l (0x4,SP),A0` that falls through into ADVANCE_INT;
 *   0x00E27070 calls 0x00E206EE directly.
 * That is exactly what an interrupt-level twin of a base-level routine looks
 * like.
 *
 * SMD_$DISP1_INT reaches the first with `jsr (0xa8,PC)` at 0x00E26F8C, which
 * is why the label has to sit at this offset.
 *
 * Entry:
 *   A0 - the BLT register block (smd_display_hw_t.blt_regs)
 *   A1 - the display hardware record (smd_display_hw_t)
 * Exit:
 *   D0.w - the BLT control word to write: 0x0C for a vertical step,
 *          0x04 for a horizontal one
 *   D1.w - 2 while more than one step remains, 1 on the last step
 *
 * Register block (A0):     +0x00 control/status (bit 15 = busy)
 *                          +0x02 bit position within the word (x & 0xF)
 *                          +0x04 destination Y   +0x06 destination X
 *                          +0x08 height - 1      +0x0A width - 1
 *                          +0x0C source Y        +0x0E source X
 * Hardware record (A1):    +0x10 op_ec           +0x24 field_24
 *                          +0x26 x1  +0x28 y1  +0x2A x2  +0x2C y2
 *                          +0x2E remaining steps +0x30 direction
 *
 * Transcribed byte for byte from 0x00E27036..0x00E27209 and objdump-compared.
 */
smd_$disp1_setup_blt:
        tst.w   (%a0)                   /* 00e27036  4a50                    */
        bmi.w   Lblt_in_use             /* 00e27038  6b00 01c2               */
        subq.w  #2,(0x2e,%a1)           /* 00e2703c  5569 002e               */
        blt.b   Llast_step_i            /* 00e27040  6d04                    */
        moveq   #2,%d1                  /* 00e27042  7202                    */
        bra.b   Lcheck_steps_i          /* 00e27044  6006                    */
Llast_step_i:
        moveq   #1,%d1                  /* 00e27046  7201                    */
        clr.w   (0x24,%a1)              /* 00e27048  4269 0024               */
Lcheck_steps_i:
        tst.w   (0x2e,%a1)              /* 00e2704c  4a69 002e               */
        bgt.b   Lsetup_common           /* 00e27050  6e4e                    */
        /*
         * Last step: advance the operation eventcount at IPL 7.  The SR is
         * saved and restored, so this really is a critical section and not a
         * forced IPL 0 exit.
         */
        movem.l %d0-%d1/%a0-%a1,-(%sp)  /* 00e27052  48e7 c0c0               */
        move.w  %sr,-(%sp)              /* 00e27056  40e7                    */
        ori.w   #0x700,%sr              /* 00e27058  007c 0700               */
        pea     (0x10,%a1)              /* 00e2705c  4869 0010               */
        jsr     (ADVANCE).l         /* 00e27060  4eb9 00e2 0728          */
        addq.w  #4,%sp                  /* 00e27066  584f                    */
        move.w  (%sp)+,%sr              /* 00e27068  46df                    */
        movem.l (%sp)+,%d0-%d1/%a0-%a1  /* 00e2706a  4cdf 0303               */
        bra.b   Lsetup_common           /* 00e2706e  6030                    */

/*
 * smd_$setup_scroll_blt - 0x00E27070, the base-level entry.
 */
        .globl  smd_$setup_scroll_blt
smd_$setup_scroll_blt:
        tst.w   (%a0)                   /* 00e27070  4a50                    */
        bmi.w   Lblt_in_use             /* 00e27072  6b00 0188               */
        subq.w  #2,(0x2e,%a1)           /* 00e27076  5569 002e               */
        blt.b   Llast_step_b            /* 00e2707a  6d04                    */
        moveq   #2,%d1                  /* 00e2707c  7202                    */
        bra.b   Lcheck_steps_b          /* 00e2707e  6006                    */
Llast_step_b:
        moveq   #1,%d1                  /* 00e27080  7201                    */
        clr.w   (0x24,%a1)              /* 00e27082  4269 0024               */
Lcheck_steps_b:
        tst.w   (0x2e,%a1)              /* 00e27086  4a69 002e               */
        bgt.b   Lsetup_common           /* 00e2708a  6e14                    */
        movem.l %d0-%d1/%a0-%a1,-(%sp)  /* 00e2708c  48e7 c0c0               */
        pea     (0x10,%a1)              /* 00e27090  4869 0010               */
        jsr     (EC_$ADVANCE).l         /* 00e27094  4eb9 00e2 06ee          */
        addq.w  #4,%sp                  /* 00e2709a  584f                    */
        movem.l (%sp)+,%d0-%d1/%a0-%a1  /* 00e2709c  4cdf 0303               */

/*
 * The shared body: switch on the scroll direction at hw+0x30.
 */
Lsetup_common:
        move.w  (0x30,%a1),%d0          /* 00e270a0  3029 0030               */
        movem.l %d2-%d3,-(%sp)          /* 00e270a4  48e7 3000               */
        .short  0xb07c, 0x0000          /* 00e270a8  cmp.w #0,D0 (CMP form)  */
        beq.b   Ldir0                   /* 00e270ac  6720                    */
        .short  0xb07c, 0x0001          /* 00e270ae  cmp.w #1,D0             */
        beq.b   Ldir1                   /* 00e270b2  676a                    */
        .short  0xb07c, 0x0002          /* 00e270b4  cmp.w #2,D0             */
        beq.w   Ldir2                   /* 00e270b8  6700 00b4               */
        .short  0xb07c, 0x0003          /* 00e270bc  cmp.w #3,D0             */
        beq.w   Ldir3                   /* 00e270c0  6700 00ea               */
        /* Anything else is a protocol error; CRASH_SYSTEM never returns. */
        pea     SMD_Invalid_Direction_From_SM_Err(%pc) /* 00e270c4  487a ff60 */
        jsr     (CRASH_SYSTEM).l        /* 00e270c8  4eb9 00e1 e700          */

/* Direction 0 - scroll down: destination Y is above the source. */
Ldir0:
        move.w  (0x26,%a1),%d0          /* 00e270ce  3029 0026               */
        move.w  %d0,(0x6,%a0)           /* 00e270d2  3140 0006               */
        move.w  %d0,(0xe,%a0)           /* 00e270d6  3140 000e               */
        move.w  (0x28,%a1),%d2          /* 00e270da  3429 0028               */
        move.w  %d2,%d3                 /* 00e270de  3602                    */
        .short  0xc67c, 0x000f          /* 00e270e0  and.w #0xf,D3 (AND form)*/
        move.w  %d3,(0x2,%a0)           /* 00e270e4  3143 0002               */
        lsr.w   #4,%d0                  /* 00e270e8  e848                    */
        lsr.w   #4,%d2                  /* 00e270ea  e84a                    */
        sub.w   %d0,%d2                 /* 00e270ec  9440                    */
        blt.b   Ldir0_pos               /* 00e270ee  6d02                    */
        neg.w   %d2                     /* 00e270f0  4442                    */
Ldir0_pos:
        subq.w  #1,%d2                  /* 00e270f2  5342                    */
        move.w  %d2,(0xa,%a0)           /* 00e270f4  3142 000a               */
        move.w  (0x2a,%a1),%d0          /* 00e270f8  3029 002a               */
        move.w  %d0,(0xc,%a0)           /* 00e270fc  3140 000c               */
        add.w   %d1,%d0                 /* 00e27100  d041                    */
        move.w  %d0,(0x4,%a0)           /* 00e27102  3140 0004               */
        sub.w   (0x2c,%a1),%d0          /* 00e27106  9069 002c               */
        blt.b   Ldir0_h                 /* 00e2710a  6d02                    */
        neg.w   %d0                     /* 00e2710c  4440                    */
Ldir0_h:
        subq.w  #1,%d0                  /* 00e2710e  5340                    */
        move.w  %d0,(0x8,%a0)           /* 00e27110  3140 0008               */
        move.w  #0xc,%d0                /* 00e27114  303c 000c               */
        movem.l (%sp)+,%d2-%d3          /* 00e27118  4cdf 000c               */
        rts                             /* 00e2711c  4e75                    */

/* Direction 1 - scroll up: destination Y is below the source. */
Ldir1:
        move.w  (0x26,%a1),%d0          /* 00e2711e  3029 0026               */
        move.w  %d0,(0x6,%a0)           /* 00e27122  3140 0006               */
        move.w  %d0,(0xe,%a0)           /* 00e27126  3140 000e               */
        move.w  (0x28,%a1),%d2          /* 00e2712a  3429 0028               */
        move.w  %d2,%d3                 /* 00e2712e  3602                    */
        .short  0xc67c, 0x000f          /* 00e27130  and.w #0xf,D3           */
        move.w  %d3,(0x2,%a0)           /* 00e27134  3143 0002               */
        lsr.w   #4,%d0                  /* 00e27138  e848                    */
        lsr.w   #4,%d2                  /* 00e2713a  e84a                    */
        sub.w   %d0,%d2                 /* 00e2713c  9440                    */
        blt.b   Ldir1_pos               /* 00e2713e  6d02                    */
        neg.w   %d2                     /* 00e27140  4442                    */
Ldir1_pos:
        subq.w  #1,%d2                  /* 00e27142  5342                    */
        move.w  %d2,(0xa,%a0)           /* 00e27144  3142 000a               */
        move.w  (0x2c,%a1),%d0          /* 00e27148  3029 002c               */
        move.w  %d0,(0xc,%a0)           /* 00e2714c  3140 000c               */
        sub.w   %d1,%d0                 /* 00e27150  9041                    */
        move.w  %d0,(0x4,%a0)           /* 00e27152  3140 0004               */
        sub.w   (0x2a,%a1),%d0          /* 00e27156  9069 002a               */
        blt.b   Ldir1_h                 /* 00e2715a  6d02                    */
        neg.w   %d0                     /* 00e2715c  4440                    */
Ldir1_h:
        subq.w  #1,%d0                  /* 00e2715e  5340                    */
        move.w  %d0,(0x8,%a0)           /* 00e27160  3140 0008               */
        move.w  #0x4,%d0                /* 00e27164  303c 0004               */
        movem.l (%sp)+,%d2-%d3          /* 00e27168  4cdf 000c               */
        rts                             /* 00e2716c  4e75                    */

/* Direction 2 - scroll right: destination X is to the right of the source. */
Ldir2:
        move.w  (0x26,%a1),%d0          /* 00e2716e  3029 0026               */
        move.w  %d0,(0xe,%a0)           /* 00e27172  3140 000e               */
        move.w  %d0,%d2                 /* 00e27176  3400                    */
        add.w   %d1,%d0                 /* 00e27178  d041                    */
        move.w  %d0,(0x6,%a0)           /* 00e2717a  3140 0006               */
        lsr.w   #4,%d2                  /* 00e2717e  e84a                    */
        move.w  (0x28,%a1),%d0          /* 00e27180  3029 0028               */
        sub.w   %d1,%d0                 /* 00e27184  9041                    */
        move.w  %d0,%d3                 /* 00e27186  3600                    */
        .short  0xc67c, 0x000f          /* 00e27188  and.w #0xf,D3           */
        move.w  %d3,(0x2,%a0)           /* 00e2718c  3143 0002               */
        lsr.w   #4,%d0                  /* 00e27190  e848                    */
        sub.w   %d2,%d0                 /* 00e27192  9042                    */
        blt.b   Ldir2_pos               /* 00e27194  6d02                    */
        neg.w   %d0                     /* 00e27196  4440                    */
Ldir2_pos:
        subq.w  #1,%d0                  /* 00e27198  5340                    */
        move.w  %d0,(0xa,%a0)           /* 00e2719a  3140 000a               */
        move.w  (0x2a,%a1),%d0          /* 00e2719e  3029 002a               */
        move.w  %d0,(0xc,%a0)           /* 00e271a2  3140 000c               */
        move.w  %d0,(0x4,%a0)           /* 00e271a6  3140 0004               */
        bra.b   Ldir_tail               /* 00e271aa  602c                    */

/* Direction 3 - scroll left: destination X is to the left of the source. */
Ldir3:
        move.w  (0x26,%a1),%d0          /* 00e271ac  3029 0026               */
        move.w  %d0,(0x6,%a0)           /* 00e271b0  3140 0006               */
        add.w   %d1,%d0                 /* 00e271b4  d041                    */
        move.w  %d0,(0xe,%a0)           /* 00e271b6  3140 000e               */
        lsr.w   #4,%d0                  /* 00e271ba  e848                    */
        move.w  (0x28,%a1),%d2          /* 00e271bc  3429 0028               */
        move.w  %d2,%d3                 /* 00e271c0  3602                    */
        .short  0xc67c, 0x000f          /* 00e271c2  and.w #0xf,D3           */
        move.w  %d3,(0x2,%a0)           /* 00e271c6  3143 0002               */
        lsr.w   #4,%d2                  /* 00e271ca  e84a                    */
        sub.w   %d0,%d2                 /* 00e271cc  9440                    */
        blt.b   Ldir3_pos               /* 00e271ce  6d02                    */
        neg.w   %d2                     /* 00e271d0  4442                    */
Ldir3_pos:
        /*
         * ORIGINAL ODDITY: this is `subq.l #1,D2` (5382) where the other
         * three arms use `subq.w #1,Dn`.  The following `move.w` stores only
         * the low half, so the visible effect is the same unless D2's high
         * half matters; it never does here.  Emitted as the image has it.
         */
        subq.l  #1,%d2                  /* 00e271d2  5382                    */
        move.w  %d2,(0xa,%a0)           /* 00e271d4  3142 000a               */

/* Shared by directions 2 and 3. */
Ldir_tail:
        move.w  (0x2a,%a1),%d0          /* 00e271d8  3029 002a               */
        move.w  %d0,(0xc,%a0)           /* 00e271dc  3140 000c               */
        move.w  %d0,(0x4,%a0)           /* 00e271e0  3140 0004               */
        sub.w   (0x2c,%a1),%d0          /* 00e271e4  9069 002c               */
        blt.b   Ldir_tail_h             /* 00e271e8  6d02                    */
        neg.w   %d0                     /* 00e271ea  4440                    */
Ldir_tail_h:
        subq.w  #1,%d0                  /* 00e271ec  5340                    */
        move.w  %d0,(0x8,%a0)           /* 00e271ee  3140 0008               */
        move.w  #0xc,%d0                /* 00e271f2  303c 000c               */
        movem.l (%sp)+,%d2-%d3          /* 00e271f6  4cdf 000c               */
        rts                             /* 00e271fa  4e75                    */

/*
 * The BLT was still busy when an entry was taken.  CRASH_SYSTEM does not
 * return, so the `bra.b` back to itself is dead code the compiler emitted to
 * close the block.
 */
Lblt_in_use:
        pea     SMD_Invalid_BLT_In_Use_Err(%pc) /* 00e271fc  487a fe2c       */
        jsr     (CRASH_SYSTEM).l        /* 00e27200  4eb9 00e1 e700          */
        addq.w  #4,%sp                  /* 00e27206  584f                    */
        bra.b   Lblt_in_use             /* 00e27208  60f2                    */

/*
 * A trailing longword holding the address of the base-level ADVANCE
 * (0x00E206EE).  Nothing in the image references it; it is emitted so the
 * next object keeps its address.
 */
        .long   0x00e206ee              /* 00e2720a: EC_$ADVANCE              */
