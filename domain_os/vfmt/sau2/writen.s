/*
 * vfmt/sau2/writen.s - VFMT_$WRITEN, the "write" procedure-variable thunk
 *
 * Original address: 0x00E6B0A4
 * Size: 24 bytes
 *
 * This is not a formatting routine.  In the SR10.4 symbol maps
 * (the sr10.4-install sau7..sau14 domain_os.map files) VFMT_$WRITEN is a 0x18-byte code
 * module paired with a 0x10-byte data cell that carries the aliases
 * VFMT_$WRITE2 / VFMT_$WRITE5 / VFMT_$WRITE10.  Each of those names is a
 * 16-byte procedure-variable descriptor of the form
 *
 *   +0x00  41 FA FF FE     lea (-0x2,%pc),%a0   ; A0 = the descriptor itself
 *   +0x04  4E F9 xx xx xx xx  jmp VFMT_$WRITEN  ; this thunk
 *   +0x0A  <longword>      the routine currently installed
 *   +0x0E  <word>          unused
 *
 * so a caller simply `jsr`s the descriptor and the trampoline hands its own
 * address to this thunk in A0.  The thunk then calls the installed routine
 * with (format, &args), where &args is the address of the caller's second
 * stack argument -- i.e. the argument list that VFMT_$MAIN walks.
 *
 * The one descriptor of this shape in the SAU2 image is VFMT_$WRITE10 at
 * 0x00E825F4, whose installed routine is VFMT_$WRITE (0x00E6AFE2):
 *
 *   00e825f4  41 fa ff fe 4e f9 00 e6  b0 a4 00 e6 af e2 00 00
 *
 * The sibling thunk for the format family is VFMT_$FORMATN at 0x00E6B074,
 * reached through the descriptor at 0x00E825E4 (installed routine
 * VFMT_$MAIN, 0x00E6AB2A).
 *
 * Stack on entry (SP0 = SP at the `jsr` target):
 *   (SP0+0x00) return address
 *   (SP0+0x04) arg1: format pointer
 *   (SP0+0x08) arg2 onwards: the argument list
 *
 * Because it takes its argument in A0 and computes an argument-list address
 * from SP, this is hand-written assembly and is transcribed rather than
 * translated (see CLAUDE.md).
 * ==================================================================== */

        .text
        .even

        .globl  VFMT_$WRITEN
        .globl  _VFMT_$WRITEN

VFMT_$WRITEN:
_VFMT_$WRITEN:
        move.l  %a5,-(%sp)              /* 0x00E6B0A4 save caller's A5      */
        movea.l %a0,%a5                 /* 0x00E6B0A6 A5 = descriptor       */
        pea     (0xc,%sp)               /* 0x00E6B0A8 push &args (SP0+0x08) */
        move.l  (0xc,%sp),-(%sp)        /* 0x00E6B0AC push format (SP0+0x04)*/
        movea.l (0xa,%a5),%a0           /* 0x00E6B0B0 A0 = installed routine*/
        jsr     (%a0)                   /* 0x00E6B0B4                       */
        addq.l  #8,%sp                  /* 0x00E6B0B6 drop the two arguments*/
        movea.l (%sp)+,%a5              /* 0x00E6B0B8 restore A5            */
        rts                             /* 0x00E6B0BA                       */
