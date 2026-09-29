/*
 * misc/sau2/crash_system.s - CRASH_SYSTEM and the crash console (SAU2)
 *
 * Byte gate (source-6psc; tools/asm_compare.py, `make check'): encodings
 * identical to the image (modulo the documented widenings); address
 * operands resolve to our objects.
 *
 * Faithful transcription of the hand-written assembly at 0x00E1E700..0x00E1E863.
 * These routines cannot be expressed in C: they save and restore the whole
 * register file with `movem` (including A7), read the USP with `movec`, take
 * their arguments in registers, manipulate SR directly and end in `trap #15`.
 *
 * The crash report block (CRASH_REPORT) and the SR stash (CRASH_SAVED_SR) are
 * defined in misc/crash_system.c and shared with the host build; in the image
 * they live at 0x00E1E97E and 0x00E1E7B6 and are reached through the module
 * base A5 = 0x00E1E700 (`lea (-0xc,PC),A5`) or PC-relative.  Since the code is
 * relinked, this file names them instead of using those displacements; the
 * original displacement is given in the comment on each access.  The crash
 * record the image writes at 0x00E00000 (map `D E00000 CRASH_RECORD size =
 * C') is CRASH_$RECORD, log/log_data.c, likewise named (source-6psc).
 * tools/asm_compare.py (`make check') checks the result: encodings identical
 * to the image except the documented A5 rebase and widenings; address
 * operands resolve to our objects.  REMAP_DISPLAY's 0x00FC0000 is the
 * display frame buffer VA (arch/m68k/sau2/hw.h SAU2_DISPLAY_MEM_BASE),
 * legitimately absolute.
 *
 *   0x00E1E700  CRASH_SYSTEM
 *   0x00E1E7B8  CRASH_SHOW_STRING
 *   0x00E1E7C8  crash_puts_string          (argument in A0)
 *   0x00E1E812  call_prom_putc             (character in D1)
 *   0x00E1E822  call_prom_reload_font
 *   0x00E1E82A  REMAP_DISPLAY
 */

        .section ".text.CRASH_SYSTEM","ax",@progbits

/* -------------------------------------------------------------------------
 * CRASH_SYSTEM - 0x00E1E700, 182 bytes
 *
 * Pascal/C entry: 4(SP) holds a status_$t *.  After the entry sequence the
 * frame is
 *   0x00(SP) .. 0x3F(SP)  the 16 saved registers, D0 lowest, A7 highest
 *   0x40(SP)              the SR pushed at entry (a word)
 *   0x42(SP)              the caller's return address  -> CRASH_REPORT.pc
 *   0x46(SP)              the status_$t * argument
 * ------------------------------------------------------------------------- */
    .globl  CRASH_SYSTEM
    .globl  _CRASH_SYSTEM
CRASH_SYSTEM:
_CRASH_SYSTEM:
    move.w  %sr, -(%sp)                     /* 0xE1E700 */
    ori.w   #0x0700, %sr                    /* 0xE1E702: IPL 7 */
    movem.l %d0-%d7/%a0-%a7, -(%sp)         /* 0xE1E706: 64 bytes */
    lea     CRASH_REPORT, %a5               /* 0xE1E70A: lea (-0xc,PC),A5 */

    movea.l 0x46(%sp), %a0                  /* 0xE1E70E: the status pointer */
    move.l  (%a0), 0x10(%a5)                /* 0xE1E712: (0x28e,A5) = *status_p */
    beq.b   Lcrash_no_message               /* 0xE1E716: status 0 -> shutdown */
    cmpi.l  #0x001b0008, 0x10(%a5)          /* 0xE1E718: clean reboot? */
    beq.b   Lcrash_no_message
    move.l  0x42(%sp), 0x1a(%a5)            /* 0xE1E722: (0x298,A5) = return addr */
    move.w  PROC1_$CURRENT, 0x24(%a5)       /* 0xE1E728: (0x2a2,A5), a WORD */
    lea     CRASH_REPORT, %a0               /* 0xE1E730: lea (0x24c,PC),A0 */
    bsr.w   crash_puts_string               /* 0xE1E734 */

Lcrash_no_message:
    lea     (CRASH_$RECORD).l, %a1          /* 0xE1E738: lea (0x00e00000).l,A1 - the crash record (log/log_data.c) */
    clr.l   (%a1)                           /* 0xE1E73E */
    tst.l   0x10(%a5)                       /* 0xE1E740 */
    beq.b   Lcrash_save_regs
    cmpi.l  #0x001b0008, 0x10(%a5)          /* 0xE1E746 */
    beq.b   Lcrash_save_regs
    move.l  #0xabcdef01, (%a1)+             /* 0xE1E750: magic */
    move.l  TIME_$CLOCKH, (%a1)+            /* 0xE1E756 */
    move.l  0x10(%a5), (%a1)                /* 0xE1E75C: move.l (0x230,PC),(A1) */

Lcrash_save_regs:
    lea     (%sp), %a0                      /* 0xE1E760: the movem block */
    lea     CRASH_REPORT+0x28, %a1          /* 0xE1E762: lea (0x242,PC),A1 */
    moveq   #15, %d0                        /* 0xE1E766: 16 longwords */
Lcrash_copy_reg:
    move.l  (%a0)+, (%a1)+                  /* 0xE1E768 */
    dbf     %d0, Lcrash_copy_reg            /* 0xE1E76A */
    movec   %usp, %d0                       /* 0xE1E76E */
    move.l  %d0, 0x6c(%a5)                  /* 0xE1E772: (0x2ea,A5) */
    jsr     KBD_$RESET                      /* 0xE1E776 */

    tst.l   0x10(%a5)                       /* 0xE1E77C */
    bne.b   Lcrash_enter_debugger
    /*
     * Clean shutdown: hand control back to the PROM through the vector at
     * absolute address 0x11C.  That cell is PROM_$QUIET_RET_ADDR (prom/prom.h)
     * -- the routine jumps to its *contents*, so the C declaration
     * `extern void *PROM_$QUIET_RET_ADDR;` names the vector word itself.
     */
    movea.l 0x0000011c, %a0                 /* 0xE1E782: movea.l (0x11c).w,A0 */
    jmp     (%a0)                           /* 0xE1E786: does not return */

Lcrash_enter_debugger:
    move.w  0x40(%sp), CRASH_SAVED_SR       /* 0xE1E788: (0xb6,A5) = entry SR */
    movem.l (%sp)+, %d0-%d7/%a0-%a7         /* 0xE1E78E: restore, SP += 64 */
    addq.w  #2, %sp                         /* 0xE1E792: drop the SR word */
    trap    #15                             /* 0xE1E794: into the debugger */

    /* Reached only if the trap #15 handler returns. */
    jsr     KBD_$CRASH_INIT                 /* 0xE1E796 */
    movem.l %d0/%a0, -(%sp)                 /* 0xE1E79C */
    lea     CRASH_REPORT+0x28, %a0          /* 0xE1E7A0: lea (0x204,PC),A0 */
    moveq   #15, %d0                        /* 0xE1E7A4 */
Lcrash_clear_reg:
    clr.l   (%a0)+                          /* 0xE1E7A6 */
    dbf     %d0, Lcrash_clear_reg           /* 0xE1E7A8 */
    movem.l (%sp)+, %d0/%a0                 /* 0xE1E7AC */
    move.w  CRASH_SAVED_SR, %sr             /* 0xE1E7B0: move (0x4,PC),SR */
    rts                                     /* 0xE1E7B4 */

/* -------------------------------------------------------------------------
 * CRASH_SHOW_STRING - 0x00E1E7B8, 16 bytes
 *
 * C-callable: 4(SP) is the string, which after the 64-byte movem is 0x44(SP).
 * ------------------------------------------------------------------------- */
    .globl  CRASH_SHOW_STRING
    .globl  _CRASH_SHOW_STRING
CRASH_SHOW_STRING:
_CRASH_SHOW_STRING:
    movem.l %d0-%d7/%a0-%a7, -(%sp)         /* 0xE1E7B8 */
    movea.l 0x44(%sp), %a0                  /* 0xE1E7BC */
    bsr.b   crash_puts_string               /* 0xE1E7C0 */
    movem.l (%sp)+, %d0-%d7/%a0-%a7         /* 0xE1E7C2 */
    rts                                     /* 0xE1E7C6 */

/* -------------------------------------------------------------------------
 * crash_puts_string - 0x00E1E7C8, 74 bytes
 *
 * A0 = format string.  Register convention, NOT C-callable.
 *   byte 0x01..0x24, 0x26..0x7F  emitted literally
 *   byte 0x25 ('%')              emit CR LF and return
 *   byte 0x00                    the next word is emitted as 4 hex digits
 *   byte 0x80..0xFF              the next long is emitted as 8 hex digits
 * Clobbers D0/D1/D2/A0 (call_prom_putc preserves exactly those).
 * ------------------------------------------------------------------------- */
    .globl  crash_puts_string
    .globl  _crash_puts_string
crash_puts_string:
_crash_puts_string:
    move.l  %a0, -(%sp)                     /* 0xE1E7C8 */
    bsr.w   REMAP_DISPLAY                   /* 0xE1E7CA */
    bsr.b   call_prom_reload_font           /* 0xE1E7CE */
    movea.l (%sp)+, %a0                     /* 0xE1E7D0 */
Lputs_next:
    move.b  (%a0)+, %d1                     /* 0xE1E7D2 */
    ble.b   Lputs_hex                       /* 0xE1E7D4: byte <= 0 */
    cmpi.b  #0x25, %d1                      /* 0xE1E7D6 */
    beq.b   Lputs_end
    bsr.b   call_prom_putc                  /* 0xE1E7DC */
    bra.b   Lputs_next
Lputs_end:
    moveq   #13, %d1                        /* 0xE1E7E0: CR */
    bsr.b   call_prom_putc
    moveq   #10, %d1                        /* 0xE1E7E4: LF */
    bsr.b   call_prom_putc
    rts                                     /* 0xE1E7E8 */
Lputs_hex:
    blt.b   Lputs_long                      /* 0xE1E7EA: negative byte */
    move.w  (%a0)+, %d2                     /* 0xE1E7EC: 16-bit field */
    swap    %d2
    moveq   #3, %d0                         /* 4 nibbles */
    bra.b   Lputs_nibble
Lputs_long:
    move.l  (%a0)+, %d2                     /* 0xE1E7F4: 32-bit field */
    moveq   #7, %d0                         /* 8 nibbles */
Lputs_nibble:
    rol.l   #4, %d2                         /* 0xE1E7F8 */
    moveq   #15, %d1
    and.b   %d2, %d1
    cmpi.b  #10, %d1
    blt.b   Lputs_digit
    addq.b  #7, %d1                         /* 0xE1E804: 'A'-'0'-10 */
Lputs_digit:
    addi.b  #0x30, %d1                      /* 0xE1E806 */
    bsr.b   call_prom_putc
    dbf     %d0, Lputs_nibble               /* 0xE1E80C */
    bra.b   Lputs_next                      /* 0xE1E810 */

/* -------------------------------------------------------------------------
 * call_prom_putc - 0x00E1E812, 16 bytes
 *
 * D1 = character.  Calls the PROM putc through the vector at address 0x108,
 * preserving exactly the registers crash_puts_string keeps live.
 * ------------------------------------------------------------------------- */
    .globl  call_prom_putc
call_prom_putc:
    movem.l %d0-%d2/%a0, -(%sp)             /* 0xE1E812 */
    movea.l 0x00000108, %a0                 /* 0xE1E816 */
    jsr     (%a0)                           /* 0xE1E81A */
    movem.l (%sp)+, %d0-%d2/%a0             /* 0xE1E81C */
    rts                                     /* 0xE1E820 */

/* -------------------------------------------------------------------------
 * call_prom_reload_font - 0x00E1E822, 6 bytes
 *
 * Tail-jumps to the PROM font reload routine (vector at address 0x114).
 * ------------------------------------------------------------------------- */
    .globl  call_prom_reload_font
call_prom_reload_font:
    movea.l 0x00000114, %a1                 /* 0xE1E822 */
    jmp     (%a1)                           /* 0xE1E826 */

/* -------------------------------------------------------------------------
 * REMAP_DISPLAY - 0x00E1E82A, 58 bytes
 *
 * Re-establishes the display mapping so the crash console is visible even if
 * the normal mappings are gone: physical pages 0x80..0xFF are installed at
 * virtual 0x00FC0000 upward, 0x400 bytes apart.
 *
 * MMU_$INSTALL is called with four by-value arguments pushed right to left:
 *   (SP)     long  physical page number, incremented in place by the loop
 *   4(SP)    long  virtual address, advanced by 0x400 in place
 *   8(SP)    word  0
 *   10(SP)   word  0x26
 * The `move SR` / `move (SP)+,SR` pair is a true save/restore.
 * ------------------------------------------------------------------------- */
    .globl  REMAP_DISPLAY
REMAP_DISPLAY:
    move.w  %sr, -(%sp)                     /* 0xE1E82A */
    ori.w   #0x0700, %sr                    /* 0xE1E82C */
    move.w  #0x26, -(%sp)                   /* 0xE1E830 */
    clr.w   -(%sp)                          /* 0xE1E834 */
    move.l  #0x00fc0000, -(%sp)             /* 0xE1E836 */
    move.l  #0x00000080, -(%sp)             /* 0xE1E83C */
Lremap_loop:
    jsr     MMU_$INSTALL                    /* 0xE1E842 */
    addq.l  #1, (%sp)                       /* 0xE1E848: next page */
    cmpi.l  #0x00000100, (%sp)              /* 0xE1E84A */
    beq.b   Lremap_done
    addi.l  #0x00000400, 4(%sp)             /* 0xE1E852: next virtual page */
    bra.b   Lremap_loop                     /* 0xE1E85A */
Lremap_done:
    adda.w  #12, %sp                        /* 0xE1E85C: drop the arguments */
    move.w  (%sp)+, %sr                     /* 0xE1E860 */
    rts                                     /* 0xE1E862 */
