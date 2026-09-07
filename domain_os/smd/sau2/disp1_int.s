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
        jsr     (ADVANCE_INT).l         /* 00e26fe6                          */
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
 * smd_$disp1_setup_blt - 0x00E27036, the interrupt-level scroll-BLT setup
 * subroutine Lnext_step calls.  It is the IPL-raising twin of the routine at
 * 0x00E27070 that smd/sau2/scroll_blt_setup.s models: the two share the
 * "subq.w #2,(0x2e,A1)" step accounting and the four-way direction switch, but
 * 0x00E27036 wraps its eventcount advance in `move SR,-(SP)` / `ori #0x700,SR`
 * and calls 0x00E20728, while 0x00E27070 calls 0x00E206EE.
 *
 * TODO(source-k6h0): the body (0x00E27036..0x00E2720D) is not emitted yet.
 * The label is defined here, at exactly the offset the image puts it, so that
 * Lnext_step's `jsr (0xa8,PC)` keeps the original displacement; when the body
 * is transcribed it goes here and nothing else moves.
 */
smd_$disp1_setup_blt:
