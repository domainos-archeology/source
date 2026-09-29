/*
 * smd/sau2/blink_cursor_1.s - SMD_$BLINK_CURSOR_1 / SMD_$BLINK_CURSOR
 *                             (hand-written assembly)
 *
 * Original addresses (SAU2 map domain_os.10.2.map, segment
 * "D E26F20 SMD_WIRED size = 5E0"):
 *   SMD_$BLINK_CURSOR_1   0x00E2722C, 0x54 bytes (..0x00E2727F)
 *   SMD_$BLINK_CURSOR     0x00E27280, 4 bytes    (..0x00E27283)
 * SMD_$INTERRUPT_INIT begins at 0x00E27284.
 *
 * Not compiler output (bead source-vk6g):
 *
 *   - no link/unlk and no frame at all;
 *   - the body is bracketed by "move SR,-(SP)" / "ori #0x700,SR" at
 *     0x00E27234 and "move (SP)+,SR" at 0x00E27278, i.e. it runs at IPL 7
 *     with the caller's SR pushed BELOW the movem'd registers, so the stack
 *     is not a shape any Domain Pascal frame takes;
 *   - A5 is loaded twice with the same value from two different `lea (d,PC)`
 *     instructions (0x00E27230 and 0x00E2723A, 0x00E27232-0x312 and
 *     0x00E2723C-0x31C are both 0x00E26F20) and then never read;
 *   - D2 and A3 are saved by the movem and never touched.
 *
 * SMD_$BLINK_CURSOR is the plain `bsr.b` thunk the map names at 0x00E27280;
 * it is emitted here so the SMD_WIRED block stays contiguous from
 * SMD_$BLINK_CURSOR_1 through SMD_$INTERRUPT_INIT.
 *
 * What it does: with interrupts off, if unit 1's controller status word is
 * non-negative, call SMD_$XOR_CURSOR to toggle unit 1's cursor image and
 * then invert SMD_TIME_$COM.cursor_painted.  SMD_$BLINK_CURSOR_CALLBACK
 * reaches it through smd_globals_t.blink_func (the cell at 0x00E84930 holds
 * 0x00E2722C) and re-tests that same flag at 0x00E6FF8E.
 *
 * The PC-relative operands, all resolved as instruction + 2 + d16:
 *   0x00E27230  lea (-0x312,PC),A5  -> 0x00E26F20  SMD_$DISP1_INT
 *   0x00E2723A  lea (-0x31c,PC),A5  -> 0x00E26F20  SMD_$DISP1_INT
 *   0x00E27248  lea (0x18e,PC),A1   -> 0x00E273D8  SMD_TIME_$COM + 2
 *                                                  (.cursor_painted)
 *   0x00E2724C  lea (0x128,PC),A0   -> 0x00E27376  SMD_$DISPLAY_COM
 *                                                  (= &SMD_DISPLAY_INFO[0])
 *   0x00E2726E  jsr (-0x62,PC)      -> 0x00E2720E  SMD_$XOR_CURSOR
 *   0x00E27280  bsr.b (-0x56,PC)    -> 0x00E2722C  SMD_$BLINK_CURSOR_1
 *
 * SMD_$XOR_CURSOR's seven arguments, pushed right to left (the callee is
 * reached through smd/sau2/cursor_thunks.s and the caller cleans up with
 * "adda.w #0x1c,SP"):
 *   0x00E2726A  pea (0x36,A0)   &SMD_DISPLAY_INFO[0].hw.cursor_number
 *   0x00E27266  pea (0x32,A0)   &SMD_DISPLAY_INFO[0].hw.cursor_pos
 *   0x00E27262  pea (0x4e,A0)   &SMD_DISPLAY_INFO[0].hw.min_x
 *   0x00E27260  pea (A0)        &SMD_DISPLAY_INFO[0]
 *   0x00E2725E  pea (A1)        &SMD_TIME_$COM.cursor_painted
 *   0x00E27256  #0x00FC0000     unit 1's display memory base, BY VALUE
 *   0x00E27250  #0x00FF9800     unit 1's controller registers, BY VALUE
 *
 * The two immediates are the same longwords SMD_$INIT plants in unit 1's
 * record at +0x108 and +0xFC, and 0x00FF9800 is also the register the
 * "tst.w" probe at 0x00E2723E reads directly.
 *
 * Byte comparison against the image ("gsk read 0xE2722C 0x58"):
 *
 *   m68k-elf-as -m68020, then objcopy -O binary          -> 0x58 bytes, all
 *   equal except the five R_68K_PC16 displacement fields, which the object
 *   leaves zero (file offsets 6, 0x10, 0x1E, 0x22 and 0x44);
 *
 *   the same object linked at its image address, which fills those five in
 *
 *     m68k-elf-ld -Ttext=0xE2722C
 *       --defsym 'SMD_$DISP1_INT'=0xE26F20 --defsym 'SMD_TIME_$COM'=0xE273D6
 *       --defsym SMD_DISPLAY_INFO=0xE27376 --defsym 'SMD_$XOR_CURSOR'=0xE2720E
 *
 *   -> all 0x58 bytes identical to the image.  gas needed no `.short`
 *   escapes here: the only encodings it could have widened are the five
 *   PC-relative operands, and the ":w" suffix pins each to the brief form.
 */

        .section ".text.SMD_$BLINK_CURSOR_1","ax",@progbits
        .globl  SMD_$BLINK_CURSOR_1
        .globl  SMD_$BLINK_CURSOR

/* Unit 1's controller register block and display memory, wired addresses. */
        .set    SMD_UNIT1_CTRL_REGS,    0x00FF9800
        .set    SMD_UNIT1_DISPLAY_BASE, 0x00FC0000

SMD_$BLINK_CURSOR_1:
        movem.l %d2/%a2-%a3/%a5,-(%sp)          /* 00e2722c  48e7 2034     */
        lea     (SMD_$DISP1_INT:w,%pc),%a5      /* 00e27230  4bfa fcee     */
        move.w  %sr,-(%sp)                      /* 00e27234  40e7          */
        ori.w   #0x700,%sr                      /* 00e27236  007c 0700     */
        lea     (SMD_$DISP1_INT:w,%pc),%a5      /* 00e2723a  4bfa fce4     */
        tst.w   SMD_UNIT1_CTRL_REGS             /* 00e2723e  4a79 00ff9800 */
        bmi.w   Ldone                           /* 00e27244  6b00 0032     */

        lea     (SMD_TIME_$COM+2:w,%pc),%a1     /* 00e27248  43fa 018e     */
        lea     (SMD_DISPLAY_INFO:w,%pc),%a0    /* 00e2724c  41fa 0128     */
        move.l  #SMD_UNIT1_CTRL_REGS,-(%sp)     /* 00e27250  2f3c 00ff9800 */
        move.l  #SMD_UNIT1_DISPLAY_BASE,-(%sp)  /* 00e27256  2f3c 00fc0000 */
        movea.l %a1,%a2                         /* 00e2725c  2449          */
        pea     (%a1)                           /* 00e2725e  4851          */
        pea     (%a0)                           /* 00e27260  4850          */
        pea     (0x4e,%a0)                      /* 00e27262  4868 004e     */
        pea     (0x32,%a0)                      /* 00e27266  4868 0032     */
        pea     (0x36,%a0)                      /* 00e2726a  4868 0036     */
        jsr     (SMD_$XOR_CURSOR:w,%pc)         /* 00e2726e  4eba ff9e     */
        adda.w  #0x1c,%sp                       /* 00e27272  defc 001c     */
        not.b   (%a2)                           /* 00e27276  4612          */

Ldone:
        move.w  (%sp)+,%sr                      /* 00e27278  46df          */
        movem.l (%sp)+,%d2/%a2-%a3/%a5          /* 00e2727a  4cdf 2c04     */
        rts                                     /* 00e2727e  4e75          */

/*
 * SMD_$BLINK_CURSOR - the map's 4-byte entry at 0x00E27280.  A `bsr.b` to
 * the routine above followed by an `rts`; nothing in the image reaches it.
 */
SMD_$BLINK_CURSOR:
        bsr.b   SMD_$BLINK_CURSOR_1             /* 00e27280  61aa          */
        rts                                     /* 00e27282  4e75          */
