/*
 * smd/sau2/lock_display.s - SMD_$LOCK_DISPLAY (hand-written assembly)
 *
 * Original address: 0x00E15CCE, 68 bytes.
 *
 * This routine is not compiler output: it has no link/unlk, it reads its two
 * arguments straight off the stack at (0x4,SP) and (0x8,SP), it brackets its
 * body with "ori #0x700,SR" / "andi #0xf8ff,SR" (a *forced* IPL 0, not an SR
 * restore), and it returns a Domain boolean in the low byte of D0 while
 * leaving the rest of D0 holding the lock state word it read.  Per CLAUDE.md
 * it therefore lives here rather than in C.
 *
 * Entry (Pascal calling sequence, caller cleans up):
 *   (0x4,SP)  smd_display_hw_t *hw
 *   (0x8,SP)  int16_t *lock_data
 *
 * Exit:
 *   D0.b  0xFF when the lock was taken, 0x00 when it was not.
 *
 * State machine (hw->lock_state is the word at hw+0x02):
 *   0 (unlocked)    -> 5, success
 *   3 (scroll done) -> 4 and lock_data[0x12] (i.e. +0x24) cleared, but only
 *                      when lock_data[0] == 1; success
 *   anything else   -> unchanged, failure
 *
 * Transcribed instruction for instruction from the listing below:
 *   00e15cce    movea.l (0x4,SP),A0
 *   00e15cd2    ori #0x700,SR
 *   00e15cd6    move.w (0x2,A0),D0w
 *   00e15cda    cmp.w #0x0,D0w
 *   00e15cde    bne.b 0x00e15ce8
 *   00e15ce0    move.w #0x5,(0x2,A0)
 *   00e15ce6    bra.b 0x00e15d0a
 *   00e15ce8    cmp.w #0x3,D0w
 *   00e15cec    bne.b 0x00e15cf8
 *   00e15cee    movea.l (0x8,SP),A1
 *   00e15cf2    cmpi.w #0x1,(A1)
 *   00e15cf6    beq.b 0x00e15d00
 *   00e15cf8    andi #-0x701,SR
 *   00e15cfc    clr.b D0b
 *   00e15cfe    rts
 *   00e15d00    move.w #0x4,(0x2,A0)
 *   00e15d06    clr.w (0x24,A1)
 *   00e15d0a    andi #-0x701,SR
 *   00e15d0e    st D0b
 *   00e15d10    rts
 *
 * Verified with m68k-elf-gcc -c + m68k-elf-objdump -d: 0x44 = 68 bytes, the
 * same size as the original, and every byte matches except the two compares.
 * The original encodes them as CMP.W #imm,D0 (0xB07C), while GNU as
 * normalises "cmp.w #imm,%d0" to CMPI.W #imm,D0 (0x0C40); both are four bytes
 * and set the flags identically.
 */

        .text
        .globl  SMD_$LOCK_DISPLAY

SMD_$LOCK_DISPLAY:
        movea.l 0x4(%sp),%a0            /* 00e15cce: A0 = hw            */
        ori.w   #0x700,%sr              /* 00e15cd2: IPL 7              */
        move.w  0x2(%a0),%d0            /* 00e15cd6: D0 = hw->lock_state*/
        cmp.w   #0x0,%d0                /* 00e15cda                     */
        bne.b   Lnot_unlocked           /* 00e15cde -> 00e15ce8         */
        move.w  #0x5,0x2(%a0)           /* 00e15ce0: state = 5          */
        bra.b   Lsucceed                /* 00e15ce6 -> 00e15d0a         */

Lnot_unlocked:
        cmp.w   #0x3,%d0                /* 00e15ce8                     */
        bne.b   Lfail                   /* 00e15cec -> 00e15cf8         */
        movea.l 0x8(%sp),%a1            /* 00e15cee: A1 = lock_data     */
        cmpi.w  #0x1,(%a1)              /* 00e15cf2                     */
        beq.b   Ltake_scroll            /* 00e15cf6 -> 00e15d00         */

Lfail:
        andi.w  #0xf8ff,%sr             /* 00e15cf8: force IPL 0        */
        clr.b   %d0                     /* 00e15cfc: return false       */
        rts                             /* 00e15cfe                     */

Ltake_scroll:
        move.w  #0x4,0x2(%a0)           /* 00e15d00: state = 4          */
        clr.w   0x24(%a1)               /* 00e15d06                     */

Lsucceed:
        andi.w  #0xf8ff,%sr             /* 00e15d0a: force IPL 0        */
        st      %d0                     /* 00e15d0e: return true (0xFF) */
        rts                             /* 00e15d10                     */
