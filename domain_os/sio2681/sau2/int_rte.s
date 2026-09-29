/*
 * sio2681/sau2/int_rte.s - SIO2681_$INT1_RTE / SIO2681_$INT2_RTE
 *
 * Original addresses: 0x00E2DFA0 (INT1) and 0x00E2DFB0 (INT2), sharing the
 * tail at 0x00E2DFBC.  Together 0x38 bytes, 0x00E2DFA0..0x00E2DFD7.
 *
 * These are the two 2681 interrupt entry points that SIO2681_$INIT installs
 * into the m68k autovector table (0x00E334F0 copies SIO2681_$INT_VECTORS[chip]
 * into the vector slot).  They are not compiler output: they raise the
 * interrupt mask in SR, save registers with a movem thunk, load a chip's
 * SIO2681_$PTRS entry with a PC-relative lea, and leave through
 * PROC1_$INT_EXIT with a jmp rather than an rts.  Per CLAUDE.md they belong
 * here rather than in C.
 *
 * Both entries hand SIO2681_$INT the ADDRESS of the chip's 16-byte
 * SIO2681_$PTRS entry: INT1 the entry at 0x00E2DF80 (chip 1) and INT2 the one
 * at 0x00E2DF90 (chip 2).  Before that they stash the interrupted PC in that
 * entry's +0x0C field: after the movem has pushed four longwords, (0x12,SP)
 * is the PC longword of the exception frame ((0x10,SP) is the saved SR).
 *
 * Transcribed instruction for instruction from the image:
 *   00e2dfa0    ori.w #0x700,SR
 *   00e2dfa4    movem.l {D0 D1 A0 A1},-(SP)
 *   00e2dfa8    lea (-0x2a,PC),A0          ; 0x00e2dfaa - 0x2a = 0x00e2df80
 *   00e2dfac    bra.b 0x00e2dfbc
 *   00e2dfae    .word 0                    ; pad to the 0x10 boundary
 *   00e2dfb0    ori.w #0x700,SR
 *   00e2dfb4    movem.l {D0 D1 A0 A1},-(SP)
 *   00e2dfb8    lea (-0x2a,PC),A0          ; 0x00e2dfba - 0x2a = 0x00e2df90
 *   00e2dfbc    move.l (0x12,SP),(0xc,A0)
 *   00e2dfc2    jsr 0x00e2e826             ; IO_$USE_INT_STACK
 *   00e2dfc8    move.l A0,-(SP)
 *   00e2dfca    jsr 0x00e1ceec             ; SIO2681_$INT
 *   00e2dfd0    addq.w #0x4,SP
 *   00e2dfd2    jmp 0x00e208fe             ; PROC1_$INT_EXIT
 *
 * IO_$USE_INT_STACK is called with nothing pushed - it switches to the
 * interrupt stack using the return address it was given, and the arguments
 * Ghidra shows for it are not supplied here.
 *
 * The third stub in the same block, SIO6509_$INT1_RTE at 0x00E2DFE8, belongs
 * to the sio6509 driver and is not transcribed here.
 */

        .section ".text.SIO2681_$INT1_RTE","ax",@progbits
        .globl  SIO2681_$INT1_RTE
        .globl  SIO2681_$INT2_RTE

SIO2681_$INT1_RTE:
        ori.w   #0x700,%sr              /* 00e2dfa0: mask to IPL 7      */
        movem.l %d0-%d1/%a0-%a1,-(%sp)  /* 00e2dfa4                     */
        lea     (SIO2681_$PTRS:w,%pc),%a0  /* 00e2dfa8: chip 1's entry     */
        bra.b   Lsio2681_int_common     /* 00e2dfac -> 00e2dfbc         */

        .word   0                       /* 00e2dfae: alignment padding  */

SIO2681_$INT2_RTE:
        ori.w   #0x700,%sr              /* 00e2dfb0                     */
        movem.l %d0-%d1/%a0-%a1,-(%sp)  /* 00e2dfb4                     */
        lea     (SIO2681_$PTRS+0x10:w,%pc),%a0 /* 00e2dfb8: chip 2's entry */
        /* falls through */

Lsio2681_int_common:
        move.l  0x12(%sp),0xc(%a0)      /* 00e2dfbc: entry->saved_pc    */
        jsr     IO_$USE_INT_STACK       /* 00e2dfc2                     */
        move.l  %a0,-(%sp)              /* 00e2dfc8: the entry address  */
        jsr     SIO2681_$INT            /* 00e2dfca                     */
        addq.w  #4,%sp                  /* 00e2dfd0 (584f: word form) */
        jmp     PROC1_$INT_EXIT         /* 00e2dfd2: no rts             */
