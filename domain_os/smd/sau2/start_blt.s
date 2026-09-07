/*
 * smd/sau2/start_blt.s - SMD_$START_BLT (hand-written assembly)
 *
 * Original addresses: 0x00E272BC (entry trampoline, 6 bytes) and
 *                     0x00E15D1E (body, 110 bytes).
 *
 * Not compiler output, for the same reasons as smd/sau2/lock_display.s and
 * smd/sau2/bit_set.s (bead source-x81r): no link/unlk, a `movem` thunk that
 * saves exactly one register, arguments read straight off SP at (0x8,SP),
 * (0xc,SP) and (0x10,SP) with the saved-register displacement folded in by
 * hand, a dispatch base loaded into A0 by the trampoline, and a spin on a
 * memory-mapped register at the end.
 *
 * Entry (Pascal calling sequence, caller cleans up):
 *   (0x8,SP)   uint16_t *params            - 8 BLT parameter words
 *   (0xc,SP)   smd_display_hw_t *hw
 *   (0x10,SP)  volatile uint16_t *hw_regs  - the unit's ctrl_regs base
 * (displacements are +4 over the usual (0x4,SP) because of the movem push.)
 *
 * The trampoline's `lea (-0x39e,PC),A0` puts 0x00E272BE - 0x39E = 0x00E26F20
 * in A0 - the same base SMD_$INTERRUPT_INIT installs as SMD_$DISP1_INT, and
 * the symbol smd/sau2/disp1_int.s now defines.  The body immediately
 * overwrites A0 with its first argument, so the value is dead; it is kept
 * here because the original keeps it.
 *
 * Behaviour, instruction for instruction:
 *   00e272bc    lea (-0x39e,PC),A0
 *   00e272c0    jmp 0x00e15d1e.l
 *   00e15d1e    movem.l {  A2},-(SP)
 *   00e15d22    movea.l (0x8,SP),A0           ; A0 = params
 *   00e15d26    movea.l (0xc,SP),A2           ; A2 = hw
 *   00e15d2a    movea.l (0x10,SP),A1          ; A1 = hw_regs
 *   00e15d2e    move.w (0x2,A0),(0x2,A1)      ; note the 1,3,2,4,5,6,7 order
 *   00e15d34    move.w (0x6,A0),(0x6,A1)
 *   00e15d3a    move.w (0x4,A0),(0x4,A1)
 *   00e15d40    move.w (0x8,A0),(0x8,A1)
 *   00e15d46    move.w (0xa,A0),(0xa,A1)
 *   00e15d4c    move.w (0xc,A0),(0xc,A1)
 *   00e15d52    move.w (0xe,A0),(0xe,A1)
 *   00e15d58    move.w (A0),D0w               ; D0 = params[0]
 *   00e15d5a    and.w #-0x22,D0w              ; & 0xFFDE
 *   00e15d5e    or.w (0x22,A2),D0w            ; | hw->video_flags
 *   00e15d62    btst.l #0x4,D0
 *   00e15d66    beq.b 0x00e15d78              ; synchronous -> just start it
 *   00e15d68    move.w #0x1,(0x2,A2)          ; hw->lock_state = 1
 *   00e15d6e    clr.w (0x20,A2)               ; whole word at hw+0x20
 *   00e15d72    move.l (0x10,A2),(0x1c,A2)    ; hw->field_1c = hw->op_ec.value
 *   00e15d78    move.w D0w,(A1)               ; start the BLT
 *   00e15d7a    btst.l #0x4,D0
 *   00e15d7e    bne.b 0x00e15d84              ; asynchronous -> return
 *   00e15d80    tst.w (A1)                    ; spin while the busy bit (15)
 *   00e15d82    bmi.b 0x00e15d80              ; of hw_regs[0] is set
 *   00e15d84    movem.l (SP)+,{  A2}
 *   00e15d88    rts
 *
 * Verified with m68k-elf-gcc -c + m68k-elf-objcopy: the whole routine
 * assembles to 118 bytes whose only differences from
 * 0x00E272BC..0x00E272C5 plus 0x00E15D1E..0x00E15D89 are the six bytes that
 * carry relocations - the `lea`'s two-byte displacement to SMD_$DISP1_INT
 * and the `jmp`'s four-byte absolute address of the body.
 *
 * The portable C model in smd/start_blt.c is compiled only when ARCH_M68K is
 * not defined, so the two never collide at link time.
 */

        .text
        .globl  SMD_$START_BLT

SMD_$START_BLT:
/*
 * 00e272bc  41 fa fc 62   lea (-0x39e,PC),A0    ; A0 = 0x00E26F20
 * 00e272c0  4e f9 00 e1 5d 1e  jmp 0x00e15d1e.l
 *
 * 0x00E272BE - 0x39E = 0x00E26F20 = SMD_$DISP1_INT, now emitted as
 * smd/sau2/disp1_int.s, so the `lea` names it.  It is a different translation
 * unit, so the assembler leaves an R_68K_PC16 relocation and the two
 * displacement bytes only take their final value at link time; the opcode
 * word (0x41FA) and the instruction's length are unchanged.  A0 is dead
 * anyway - 0x00E15D22 overwrites it with the first argument before any use.
 *
 * The ":w" forces the brief PC-relative form; without it gas picks the 68020
 * full extension word (0x43FB ...) and the instruction grows by two bytes.
 */
        lea     (SMD_$DISP1_INT:w,%pc),%a0      /* 00e272bc                  */
        jmp     (Lstart_blt_body).l             /* 00e272c0                  */

Lstart_blt_body:
        movem.l %a2,-(%sp)              /* 00e15d1e                          */
        movea.l 0x8(%sp),%a0            /* 00e15d22: A0 = params             */
        movea.l 0xc(%sp),%a2            /* 00e15d26: A2 = hw                 */
        movea.l 0x10(%sp),%a1           /* 00e15d2a: A1 = hw_regs            */
        move.w  0x2(%a0),0x2(%a1)       /* 00e15d2e                          */
        move.w  0x6(%a0),0x6(%a1)       /* 00e15d34                          */
        move.w  0x4(%a0),0x4(%a1)       /* 00e15d3a                          */
        move.w  0x8(%a0),0x8(%a1)       /* 00e15d40                          */
        move.w  0xa(%a0),0xa(%a1)       /* 00e15d46                          */
        move.w  0xc(%a0),0xc(%a1)       /* 00e15d4c                          */
        move.w  0xe(%a0),0xe(%a1)       /* 00e15d52                          */
        move.w  (%a0),%d0               /* 00e15d58                          */
        /*
         * 00e15d5a  c0 7c ff de  and.w #-0x22,D0
         * The original uses the AND-with-immediate-source form (0xC07C); GNU
         * as normalises "and.w #imm,%d0" to ANDI.W (0x0240), which is the same
         * length and sets the same flags but different bytes.  Emitted
         * literally so the body assembles byte for byte identical.
         */
        .short  0xc07c, 0xffde          /* 00e15d5a: and.w #-0x22,%d0        */
        or.w    0x22(%a2),%d0           /* 00e15d5e                          */
        btst    #0x4,%d0                /* 00e15d62                          */
        beq.b   Lstart                  /* 00e15d66 -> 00e15d78              */
        move.w  #0x1,0x2(%a2)           /* 00e15d68                          */
        clr.w   0x20(%a2)               /* 00e15d6e                          */
        move.l  0x10(%a2),0x1c(%a2)     /* 00e15d72                          */

Lstart:
        move.w  %d0,(%a1)               /* 00e15d78                          */
        btst    #0x4,%d0                /* 00e15d7a                          */
        bne.b   Ldone                   /* 00e15d7e -> 00e15d84              */

Lspin:
        tst.w   (%a1)                   /* 00e15d80                          */
        bmi.b   Lspin                   /* 00e15d82                          */

Ldone:
        movem.l (%sp)+,%a2              /* 00e15d84                          */
        rts                             /* 00e15d88                          */
