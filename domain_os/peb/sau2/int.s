/*
 * peb/sau2/int.s - the PEB_ASM module, 0x00E24468..0x00E244F0
 *
 * The SAU2 map has this as a DATA segment, "D E24468 PEB_ASM size = 88", with
 * three interior symbols: PEB_$STATUS_REG (0x00E24468), PEB_$INT (0x00E2446C)
 * and PEB_$DISP_INT_ADDR (0x00E24478).  It is hand-written assembly:
 *
 *   - PEB_$INT has no link/unlk frame and no rts.  It saves D0/D1/A0/A1 with
 *     movem, raises the interrupt mask by writing SR directly
 *     (`ori.w #0x600,%sr` / `move.w %d0,%sr`, a forced level rather than a
 *     restore) and leaves through `jmp 0x00e208fe` -- the shared interrupt
 *     exit -- rather than returning.
 *   - PEB_$DISP_INT_ADDR is not a data cell that PEB_$INT reads: it is the
 *     32-bit operand of the `jmp <abs>.l` at 0x00E24476, so writing it
 *     re-targets that jump.  The image value is 0x00E21F20
 *     (FIM_$SPURIOUS_INT); SMD_$INTERRUPT_INIT overwrites it with
 *     SMD_$DISP1_INT when the PEB routes the display interrupt
 *     (smd/interrupt_init.c).  This is why it can only be expressed here and
 *     not in C.
 *   - The last 16 bytes are a literal pool the code reaches PC-relative.
 *
 * Semantics:
 *   1. If PEB control bit 2 at 0x00FF7001 is clear the interrupt is not ours;
 *      jump through PEB_$DISP_INT_ADDR (FIM_$SPURIOUS_INT by default).
 *   2. Touch 0x00FF73FC to acknowledge, save the scratch registers, mask to
 *      IPL 6 and switch to the interrupt stack (IO_$USE_INT_STACK).
 *   3. Latch the PEB exception status from 0x000070F4 into PEB_$STATUS_REG.
 *   4. If no exception bit (0x3F) is set, CRASH_SYSTEM(PEB_interrupt).
 *   5. Otherwise signal the waiting process through DXM_$ADD_SIGNAL, with the
 *      owner ASID byte at 0x00E24C8E and the status longword 0x00240002.
 *   6. Leave through the shared interrupt exit at 0x00E208FE.
 *
 * Original bytes (gsk read 0x00E24468 0x88):
 *   0xe24468: 00 00 00 00                  PEB_$STATUS_REG
 *   0xe2446c: 08 39 00 02 00 ff 70 01      btst.b #2,(0x00ff7001).l
 *   0xe24474: 66 06                        bne.b  0xe2447c
 *   0xe24476: 4e f9                        jmp    (PEB_$DISP_INT_ADDR).l
 *   0xe24478: 00 e2 1f 20                  PEB_$DISP_INT_ADDR
 *   0xe2447c: 4a 39 00 ff 73 fc            tst.b  (0x00ff73fc).l
 *   0xe24482: 48 e7 c0 c0                  movem.l {D0 D1 A0 A1},-(SP)
 *   0xe24486: 40 c0                        move.w SR,D0
 *   0xe24488: 00 7c 06 00                  ori.w  #0x600,SR
 *   0xe2448c: 4e b9 00 e2 e8 26            jsr    (0x00e2e826).l
 *   0xe24492: 46 c0                        move.w D0,SR
 *   0xe24494: 20 38 70 f4                  move.l (0x70f4).w,D0
 *   0xe24498: 23 c0 00 e2 44 68            move.l D0,(0x00e24468).l
 *   0xe2449e: c0 bc 00 00 00 3f            and.l  #0x3f,D0
 *   0xe244a4: 66 0c                        bne.b  0xe244b2
 *   0xe244a6: 48 7a 00 3e                  pea    (0x3e,PC)
 *   0xe244aa: 41 f9 00 e1 e7 00            lea    (0x00e1e700).l,A0
 *   0xe244b0: 4e 90                        jsr    (A0)
 *   0xe244b2: 48 7a 00 2c                  pea    (0x2c,PC)
 *   0xe244b6: 1f 3a 00 34                  move.b (0x34,PC),-(SP)
 *   0xe244ba: 2f 3a 00 2a                  move.l (0x2a,PC),-(SP)
 *   0xe244be: 3f 3a 00 24                  move.w (0x24,PC),-(SP)
 *   0xe244c2: 42 80                        clr.l  D0
 *   0xe244c4: 10 39 00 e2 4c 8e            move.b (0x00e24c8e).l,D0
 *   0xe244ca: 3f 00                        move.w D0,-(SP)
 *   0xe244cc: 3f 3a 00 1c                  move.w (0x1c,PC),-(SP)
 *   0xe244d0: 4e b9 00 e1 72 70            jsr    (0x00e17270).l
 *   0xe244d6: de fc 00 10                  adda.w #0x10,SP
 *   0xe244da: 4e f9 00 e2 08 fe            jmp    (0x00e208fe).l
 *   0xe244e0: 00 00 00 00                  literal: longword 0
 *   0xe244e4: 00 08                        literal: word 8
 *   0xe244e6: 00 24 00 02                  literal: status 0x00240002
 *   0xe244ea: 00 00                        literal: word 0
 *   0xe244ec: ff                           literal: byte 0xff
 *   0xe244ed: 00 00 00                     zero fill to 0x00e244f0
 *
 * Every cross-module reference in the original is already an absolute long,
 * so the addresses below are `.set` constants rather than `.extern` symbols
 * (the same convention as fim/sau2/fim.s); that keeps the emitted bytes
 * identical to the image regardless of where the linker places anything.
 * The one encoding gas cannot be asked for is the AND-immediate-effective-
 * address form the Apollo assembler used at 0x00E2449E (0xC0BC); gas always
 * picks ANDI (0x0280), so that instruction is emitted with .short.
 */

        .text
        .even

/* Hardware and code addresses the original encodes absolutely. */
        .set    PEB_CTL_BYTE,       0x00FF7001  /* PEB control, bit 2 = ours  */
        .set    PEB_ACK_BYTE,       0x00FF73FC  /* touched to acknowledge     */
        .set    PEB_EXC_STATUS,     0x000070F4  /* absolute short in the image*/
        .set    PEB_$OWNER_ASID_B,  0x00E24C8E  /* owner ASID byte            */
        .set    FIM_$SPURIOUS_INT,  0x00E21F20  /* default jmp target         */
        .set    IO_$USE_INT_STACK,  0x00E2E826
        .set    CRASH_SYSTEM,       0x00E1E700
        .set    DXM_$ADD_SIGNAL,    0x00E17270
        .set    FIM_$EXIT,          0x00E208FE  /* shared interrupt exit      */
        .set    PEB_EXC_MASK,       0x0000003F

        .globl  PEB_$STATUS_REG
PEB_$STATUS_REG:
        .long   0                       /* 0x00e24468: latched 0x70f4 value */

        .globl  PEB_$INT
PEB_$INT:
        btst.b  #2,(PEB_CTL_BYTE).l     /* 0x00e2446c: is the PEB asserting? */
        bne.b   .Lours                  /* 0x00e24474 */
        /*
         * 0x00e24476: `jmp <abs>.l` whose operand IS PEB_$DISP_INT_ADDR.
         * .short 0x4EF9 keeps the opcode and the label next to its operand.
         */
        .short  0x4EF9
        .globl  PEB_$DISP_INT_ADDR
PEB_$DISP_INT_ADDR:
        .long   FIM_$SPURIOUS_INT       /* 0x00e24478: patched by SMD */

.Lours:
        tst.b   (PEB_ACK_BYTE).l                /* 0x00e2447c */
        movem.l %d0-%d1/%a0-%a1,-(%sp)          /* 0x00e24482 */
        move.w  %sr,%d0                         /* 0x00e24486 */
        ori.w   #0x600,%sr                      /* 0x00e24488: force IPL 6 */
        jsr     (IO_$USE_INT_STACK).l           /* 0x00e2448c */
        move.w  %d0,%sr                         /* 0x00e24492 */
        move.l  (PEB_EXC_STATUS).w,%d0          /* 0x00e24494 */
        move.l  %d0,(PEB_$STATUS_REG).l         /* 0x00e24498 */
        /* 0x00e2449e: and.l #PEB_EXC_MASK,%d0 in the Apollo AND-immediate-EA
         * spelling (0xC0BC); gas emits ANDI (0x0280) for the same mnemonic. */
        .short  0xC0BC
        .long   PEB_EXC_MASK
        bne.b   .Lsignal                        /* 0x00e244a4 */
        pea     (.Lstatus,%pc)                  /* 0x00e244a6 */
        lea     (CRASH_SYSTEM).l,%a0            /* 0x00e244aa */
        jsr     (%a0)                           /* 0x00e244b0 */

.Lsignal:
        pea     (.Lzero,%pc)                    /* 0x00e244b2 */
        move.b  (.Lff,%pc),-(%sp)               /* 0x00e244b6 */
        move.l  (.Lstatus,%pc),-(%sp)           /* 0x00e244ba */
        move.w  (.Lw8,%pc),-(%sp)               /* 0x00e244be */
        clr.l   %d0                             /* 0x00e244c2 */
        move.b  (PEB_$OWNER_ASID_B).l,%d0       /* 0x00e244c4 */
        move.w  %d0,-(%sp)                      /* 0x00e244ca */
        move.w  (.Lw0,%pc),-(%sp)               /* 0x00e244cc */
        jsr     (DXM_$ADD_SIGNAL).l             /* 0x00e244d0 */
        adda.w  #0x10,%sp                       /* 0x00e244d6 */
        jmp     (FIM_$EXIT).l                   /* 0x00e244da */

/* Literal pool, 0x00e244e0..0x00e244ef. */
.Lzero: .long   0                       /* 0x00e244e0 */
.Lw8:   .short  8                       /* 0x00e244e4 */
.Lstatus:
        .long   0x00240002              /* 0x00e244e6: PEB_interrupt */
.Lw0:   .short  0                       /* 0x00e244ea */
.Lff:   .byte   0xFF                    /* 0x00e244ec */
        .byte   0                       /* 0x00e244ed */
        .short  0                       /* 0x00e244ee: fill to 0x00e244f0 */
