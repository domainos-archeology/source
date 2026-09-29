/*
 * smd/sau2/cursor_thunks.s - SMD_$XOR_CURSOR / SMD_$OR_CURSOR
 *                            (hand-written assembly)
 *
 * Original addresses:
 *   SMD_$XOR_CURSOR  0x00E2720E (trampoline, 10 bytes)
 *   SMD_$OR_CURSOR   0x00E27218 (trampoline, 10 bytes)
 *   two dispatch cells at 0x00E27222 and 0x00E27226
 *   the shared body   0x00E15B90..0x00E15C67 (0xD8 bytes), entered at
 *                     0x00E15B90 (XOR) and 0x00E15B9A (OR)
 *
 * Both trampolines sit in the SAU2 map's "D E26F20 SMD_WIRED size = 5E0"
 * block, immediately after the trailing longword smd/sau2/disp1_int.s emits
 * at 0x00E2720A; the body sits in that module's code segment, the map's
 * "I E15B90 SMD_WIRED size = 1FC", whose first two named symbols are
 * SMD_$LOCK_DISPLAY (0x00E15CCE) and SMD_$BIT_SET (0x00E15D12).  The map gives
 * the body no interior symbol of its own - the two entry points ARE
 * SMD_$XOR_CURSOR and SMD_$OR_CURSOR - so the body is emitted here, with the
 * trampolines that reach it, rather than in a file of its own.
 *
 * Not compiler output, for the same reasons as smd/sau2/scroll.s and
 * smd/sau2/start_blt.s (bead source-3vje): two separate entry points whose
 * prologues are duplicated and which differ only in the discriminator they
 * leave in D3, the first of them branching over the second; no link/unlk;
 * arguments read straight off SP at (0x24,SP)..(0x38,SP) with the movem
 * displacement (0x20) folded in by hand; a module base handed in through A0;
 * calls made by loading a routine address out of that base with
 * `movea.l (0x302,A5),A0` / `jsr (A0)` rather than through a named symbol;
 * and a result returned as `st D2b` / `move.l D2,D0`, which leaves the loop's
 * last shifted pattern word in the upper three bytes of the return value.
 *
 * A0 is a REGISTER ARGUMENT: each trampoline's `lea (d16,PC),A0` leaves
 * 0x00E26F20 there (0x00E27210 - 0x2F0 and 0x00E2721A - 0x2FA are the same
 * address), which is SMD_$DISP1_INT, the base smd/sau2/disp1_int.s defines.
 * The body copies it into A5 and addresses the rest of the wired block off
 * it:
 *   (0x302,A5) = 0x00E27222 -> PROC1_$SET_LOCK   (called with lock id 8)
 *   (0x306,A5) = 0x00E27226 -> PROC1_$CLR_LOCK   (called with lock id 8)
 *   (0x3a6,A5) = 0x00E272C6 =  SMD_$CURSOR_TABLE, four 0x28-byte
 *                              smd_cursor_pattern_t records (smd/smd_data.c)
 * The two dispatch cells are unnamed in the map; they are emitted below so
 * the block stays contiguous through 0x00E27229, where
 * MATROX_$BOARD_INSTALLED begins.
 *
 * Entry (Pascal calling sequence, caller cleans up; the one caller,
 * SHOW_CURSOR at 0x00E6E396..0x00E6E3B6, pushes seven longwords and then
 * does `lea (0x1c,SP),SP`):
 *   (0x24,SP)  int16_t *cursor_num          - index into SMD_$CURSOR_TABLE
 *   (0x28,SP)  uint32_t *cursor_pos         - packed {y:high, x:low}
 *   (0x2c,SP)  int16_t *bounds              - clip rectangle; +2 and +6 used
 *   (0x30,SP)  smd_display_hw_t *hw         - loaded into A0 and never used
 *   (0x34,SP)  const boolean *erase_flag    - never read by this routine
 *   (0x38,SP)  uint32_t display_base        - by value; the frame buffer
 *   (0x3c,SP)  SMD_HW_REG_PTR ctrl_regs     - by value; never read
 * (displacements are +0x20 over the usual (0x4,SP) because of the movem.)
 *
 * Exit:
 *   D0.b = 0xFF (the Domain boolean true) unconditionally; D0's upper bytes
 *   hold whatever the last `lsr.l %d1,%d2` left there.
 *
 * What it does: clamps the cursor's hot-spot-corrected position to the clip
 * rectangle, turns it into a frame-buffer address of
 *   display_base + ((y << 16 | (x << 6)) >> 9 & ~1)
 * (i.e. y * 0x80 bytes per scan line plus x / 8, forced even), then walks the
 * pattern's rows from the bottom up, shifting each 16-bit row left-aligned in
 * a longword right by (x & 0xF) and combining it with the frame buffer: EOR
 * for SMD_$XOR_CURSOR, OR for SMD_$OR_CURSOR.  The whole thing runs under
 * PROC1 lock 8.
 *
 * Verified with m68k-elf-gcc -c + m68k-elf-objcopy -O binary: the 20
 * trampoline bytes at 0x00E2720E..0x00E27221 and the 0xD8 body bytes at
 * 0x00E15B90..0x00E15C67 assemble identically except for the fields that
 * carry relocations - each `lea`'s two-byte displacement to SMD_$DISP1_INT,
 * each `jmp`'s four-byte absolute address (0x00E15B90 / 0x00E15B9A) and the
 * two dispatch longwords (0x00E20AE4 / 0x00E20B92).
 */

        .section ".text.SMD_$XOR_CURSOR","ax",@progbits
        .globl  SMD_$XOR_CURSOR
        .globl  SMD_$OR_CURSOR

/*
 * 00e2720e  41 fa fd 10   lea (-0x2f0,PC),A0    ; A0 = 0x00E26F20
 * 00e27212  4e f9 00 e1 5b 90  jmp 0x00e15b90.l
 *
 * The ":w" forces the brief PC-relative form; without it gas picks the 68020
 * full extension word (0x43FB ...) and the instruction grows by two bytes.
 */
SMD_$XOR_CURSOR:
        lea     (SMD_$DISP1_INT:w,%pc),%a0      /* 00e2720e                  */
        jmp     (Lxor_entry).l                  /* 00e27212                  */

/*
 * 00e27218  41 fa fd 06   lea (-0x2fa,PC),A0    ; A0 = 0x00E26F20
 * 00e2721c  4e f9 00 e1 5b 9a  jmp 0x00e15b9a.l
 */
        .section ".text.SMD_$OR_CURSOR","ax",@progbits
        .balign 2
SMD_$OR_CURSOR:
        lea     (SMD_$DISP1_INT:w,%pc),%a0      /* 00e27218                  */
        jmp     (Lor_entry).l                   /* 00e2721c                  */

/*
 * The two routine pointers the body reaches through (0x302,A5)/(0x306,A5).
 * Unnamed in the map, so they get no global here.
 */
Lsmd_wired_set_lock:
        .long   PROC1_$SET_LOCK         /* 00e27222: 00 e2 0a e4             */
Lsmd_wired_clr_lock:
        .long   PROC1_$CLR_LOCK         /* 00e27226: 00 e2 0b 92             */

/*
 * The shared body.  Two prologues, one for each entry point; the XOR entry
 * branches over the OR entry's copy.
 */
Lxor_entry:
        movem.l %d2-%d4/%d7/%a2-%a5,-(%sp) /* 00e15b90  48e7 393c            */
        movea.l %a0,%a5                 /* 00e15b94: A5 = SMD_WIRED base     */
        clr.b   %d3                     /* 00e15b96: D3 = 0 -> eor.l         */
        bra.b   Lbody                   /* 00e15b98 -> 00e15ba2              */

Lor_entry:
        movem.l %d2-%d4/%d7/%a2-%a5,-(%sp) /* 00e15b9a  48e7 393c            */
        movea.l %a0,%a5                 /* 00e15b9e                          */
        moveq   #1,%d3                  /* 00e15ba0: D3 = 1 -> or.l          */

Lbody:
        move.w  #0x8,-(%sp)             /* 00e15ba2                          */
        movea.l 0x302(%a5),%a0          /* 00e15ba6: -> PROC1_$SET_LOCK      */
        jsr     (%a0)                   /* 00e15baa                          */
        addq.l  #2,%sp                  /* 00e15bac                          */

        /* A1 = &SMD_$CURSOR_TABLE[*cursor_num] (0x28 bytes per record) */
        move.w  #0x28,%d0               /* 00e15bae                          */
        movea.l 0x24(%sp),%a0           /* 00e15bb2: A0 = cursor_num         */
        mulu.w  (%a0),%d0               /* 00e15bb6                          */
        lea     0x3a6(%a5),%a1          /* 00e15bb8: = SMD_$CURSOR_TABLE     */
        adda.w  %d0,%a1                 /* 00e15bbc                          */

        movea.l 0x28(%sp),%a0           /* 00e15bbe: A0 = cursor_pos         */
        move.l  (%a0),%d0               /* 00e15bc2: D0 = {y:high, x:low}    */
        movea.l 0x2c(%sp),%a2           /* 00e15bc4: A2 = bounds             */

        /* horizontal: x - pattern->hot_x, floored at 0 */
        sub.w   0x4(%a1),%d0            /* 00e15bc8                          */
        bpl.b   Lx_nonneg               /* 00e15bcc -> 00e15bd0              */
        clr.w   %d0                     /* 00e15bce                          */

Lx_nonneg:
        /* ... and capped at bounds[1] - pattern->width + 1 */
        move.w  0x2(%a2),%d1            /* 00e15bd0                          */
        sub.w   (%a1),%d1               /* 00e15bd4                          */
        addq.w  #1,%d1                  /* 00e15bd6                          */
        cmp.w   %d1,%d0                 /* 00e15bd8                          */
        ble.b   Lx_done                 /* 00e15bda -> 00e15bde              */
        move.w  %d1,%d0                 /* 00e15bdc                          */

Lx_done:
        swap    %d0                     /* 00e15bde: work on y               */

        /* vertical: y + pattern->hot_y, floored at pattern->height - 1 */
        add.w   0x6(%a1),%d0            /* 00e15be0                          */
        cmp.w   0x2(%a1),%d0            /* 00e15be4                          */
        bge.b   Ly_ok                   /* 00e15be8 -> 00e15bf0              */
        move.w  0x2(%a1),%d0            /* 00e15bea                          */
        subq.w  #1,%d0                  /* 00e15bee                          */

Ly_ok:
        /* ... and capped at bounds[3] */
        cmp.w   0x6(%a2),%d0            /* 00e15bf0                          */
        ble.b   Ly_done                 /* 00e15bf4 -> 00e15bfa              */
        move.w  0x6(%a2),%d0            /* 00e15bf6                          */

Ly_done:
        /*
         * A0 is loaded with the hw record and then overwritten at 0x00E15C18
         * before any use, and the `bra.w` goes to the instruction that
         * follows it.  Both are dead; the original keeps them, so this does
         * too.  The ".w" is required - gas would otherwise shorten the branch
         * to `bra.b` (0x6000 0002 becomes 0x6002) and the routine would be two
         * bytes short.
         */
        movea.l 0x30(%sp),%a0           /* 00e15bfa: A0 = hw (dead)          */
        bra.w   Laddr                   /* 00e15bfe -> 00e15c02              */

Laddr:
        swap    %d0                     /* 00e15c02: D0 = {y:high, x:low}    */
        move.w  #0x80,%d4               /* 00e15c04: 0x80 bytes per scan line*/
        move.w  %d0,%d1                 /* 00e15c08                          */
        /*
         * 00e15c0a  c2 7c 00 0f  and.w #0xf,D1
         * The original uses the AND-with-immediate-source form (0xC27C); GNU
         * as normalises "and.w #imm,%d1" to ANDI.W (0x0241), which is the same
         * length and sets the same flags but different bytes.  Emitted
         * literally so the body assembles byte for byte identical.
         */
        .short  0xc27c, 0x000f          /* 00e15c0a: D1 = x & 0xF            */
        lsl.w   #6,%d0                  /* 00e15c0e                          */
        lsr.l   #7,%d0                  /* 00e15c10: (y<<16 | x<<6) >> 9 ... */
        lsr.l   #2,%d0                  /* 00e15c12                          */
        bclr    #0x0,%d0                /* 00e15c14: ... forced even         */
        movea.l 0x38(%sp),%a0           /* 00e15c18: A0 = display_base       */
        adda.l  %d0,%a0                 /* 00e15c1c                          */

        /* A1 = &pattern->rows[height - 1]; D0 = height - 1 (dbf count) */
        move.w  0x2(%a1),%d0            /* 00e15c1e                          */
        subq.w  #1,%d0                  /* 00e15c22                          */
        lea     0x8(%a1),%a1            /* 00e15c24                          */
        adda.w  %d0,%a1                 /* 00e15c28                          */
        adda.w  %d0,%a1                 /* 00e15c2a                          */

        tst.b   %d3                     /* 00e15c2c                          */
        bne.b   Lor_loop                /* 00e15c2e -> 00e15c42              */

Lxor_loop:
        move.l  (%a1),%d2               /* 00e15c30: row in the high half    */
        clr.w   %d2                     /* 00e15c32                          */
        lsr.l   %d1,%d2                 /* 00e15c34                          */
        eor.l   %d2,(%a0)               /* 00e15c36                          */
        suba.w  %d4,%a0                 /* 00e15c38: previous scan line      */
        subq.w  #2,%a1                  /* 00e15c3a: previous pattern row    */
        dbf     %d0,Lxor_loop           /* 00e15c3c                          */
        bra.b   Lunlock                 /* 00e15c40 -> 00e15c52              */

Lor_loop:
        move.l  (%a1),%d2               /* 00e15c42                          */
        clr.w   %d2                     /* 00e15c44                          */
        lsr.l   %d1,%d2                 /* 00e15c46                          */
        or.l    %d2,(%a0)               /* 00e15c48                          */
        suba.w  %d4,%a0                 /* 00e15c4a                          */
        subq.w  #2,%a1                  /* 00e15c4c                          */
        dbf     %d0,Lor_loop            /* 00e15c4e                          */

Lunlock:
        st      %d2                     /* 00e15c52: low byte true, rest kept*/
        move.w  #0x8,-(%sp)             /* 00e15c54                          */
        movea.l 0x306(%a5),%a0          /* 00e15c58: -> PROC1_$CLR_LOCK      */
        jsr     (%a0)                   /* 00e15c5c                          */
        addq.l  #2,%sp                  /* 00e15c5e                          */
        move.l  %d2,%d0                 /* 00e15c60                          */
        movem.l (%sp)+,%d2-%d4/%d7/%a2-%a5 /* 00e15c62  4cdf 3c9c            */
        rts                             /* 00e15c66                          */
