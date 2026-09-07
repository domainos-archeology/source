/*
 * smd/init_display_state.c - smd_$init_display_state implementation
 *
 * Original address: 0x00E6F514 (112 bytes).  Module-local: the SAU2 map
 * (~/src/domainos-archeology/sau2-maps/domain_os.10.2.map) jumps from
 * SMD_$SEND_RESPONSE (E6F4BE) straight to SMD_$BORROW_DISPLAY (E6F584), so
 * this body exports no symbol and keeps its descriptive tree name.
 *
 * It is the entire body of the exported SMD_$INIT_STATE wrapper
 * (0x00E6F818, which calls it at 0x00E6F82C with options = 0) and is also
 * called by SMD_$BORROW_DISPLAY (0x00E6F69C, which forwards *options).
 *
 * A5: the function never loads A5 - it runs on the caller's.  Both call
 * sites set A5 to 0x00E82B8C (&SMD_GLOBALS) before calling
 * ("lea (0xe82b8c).l,A5" at 0x00E6F58C in SMD_$BORROW_DISPLAY and at
 * 0x00E6F81E in SMD_$INIT_STATE), so (0x48,A5) is
 * SMD_GLOBALS.asid_to_unit - exactly the base SMD_$VIDEO_CTL uses when it
 * loads A5 itself (0x00E6F840).
 *
 * Assembly (every instruction accounted for):
 *   00e6f514    link.w A6,-0x14                 ; 0x14 bytes of unused locals
 *   00e6f518    movem.l {  A3 A2 D3 D2},-(SP)
 *   00e6f51c    move.b (0x8,A6),D2b             ; D2 = options ("full")
 *   00e6f520    move.w (0x00e2060a).l,D0w       ; D0 = PROC1_$AS_ID
 *   00e6f526    movea.l (0xa,A6),A2             ; A2 = status_ret
 *   00e6f52a    add.w D0w,D0w                   ; word-scaled index
 *   00e6f52c    move.w (0x48,A5,D0w*0x1),D3w    ; D3 = asid_to_unit[asid]
 *   00e6f530    bne.b 0x00e6f53a
 *   00e6f532    move.l #0x130004,(A2)           ; "invalid use of display
 *                                               ;  driver procedure"
 *   00e6f538    bra.b 0x00e6f57a                ; ... and do NOT release
 *   00e6f53a    clr.l (A2)                      ; *status_ret = status_$ok
 *   00e6f53c    move.w D3w,D0w
 *   00e6f53e    movea.l #0xe2e3fc,A0
 *   00e6f544    muls.w #0x10c,D0                ; signed unit * 0x10C
 *   00e6f548    lea (0x0,A0,D0*0x1),A3          ; A3 = biased unit record
 *   00e6f54c    pea (-0x1c22,PC)                ; -> 0x00E6D92C, the constant
 *                                               ;    word 0 = SMD_ACQ_LOCK_DATA
 *   00e6f550    bsr.w 0x00e6eb42                ; SMD_$ACQ_DISPLAY
 *   00e6f554    addq.w #0x4,SP
 *   00e6f556    movea.l (0x8,A3),A0             ; A0 = rec->ctrl_regs (+0xFC)
 *   00e6f55a    move.w D0w,(A0)                 ; *ctrl_regs = acq result
 *   00e6f55c    move.b D2b,-(SP)                ; push `full`
 *   00e6f55e    move.w D3w,-(SP)                ; push unit
 *   00e6f560    bsr.w 0x00e6d736                ; smd_$reset_unit_display
 *   00e6f564    addq.w #0x4,SP
 *   00e6f566    tst.b D2b
 *   00e6f568    bpl.b 0x00e6f576                ; options >= 0 -> skip video
 *   00e6f56a    pea (A2)                        ; status_ret
 *   00e6f56c    pea (-0x1116,PC)                ; -> 0x00E6E458, the byte 0xFF
 *                                               ;    = SMD_TRUE_DATA
 *   00e6f570    bsr.w 0x00e6f838                ; SMD_$VIDEO_CTL
 *   00e6f574    addq.w #0x8,SP
 *   00e6f576    bsr.w 0x00e6ec10                ; SMD_$REL_DISPLAY
 *   00e6f57a    movem.l (-0x24,A6),{  D2 D3 A2 A3}
 *   00e6f580    unlk A6
 *   00e6f582    rts
 *
 * Note on the argument slots: the callers push the byte as the high half of
 * a word ("clr.w -(SP)" in SMD_$INIT_STATE), which is why the byte is read
 * at the even offset (0x8,A6); smd_$reset_unit_display's own `full` byte is
 * pushed the same way at 0x00E6F55C and read at (0xa,A6).
 */

#include "smd/smd_internal.h"

/*
 * smd_$init_display_state - Initialize display state for borrow/associate
 *
 * Parameters:
 *   options    - Pascal boolean "full" flag (negative = full init: also
 *                re-enable video).  Forwarded verbatim to
 *                smd_$reset_unit_display.
 *   status_ret - Status return
 */
void smd_$init_display_state(int8_t options, status_$t *status_ret)
{
    int16_t unit;
    smd_display_unit_t *rec;

    /* 0x00E6F520-0x00E6F52C: unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] */
    unit = (int16_t)SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        /* 0x00E6F532: no display associated with this process.  The
         * original returns here without acquiring or releasing anything. */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00E6F53A */
    *status_ret = status_$ok;

    /* 0x00E6F53E-0x00E6F548 */
    rec = smd_$unit_rec(unit);

    /*
     * 0x00E6F54C-0x00E6F55A: acquire the display and poke the returned
     * control word straight into the unit's controller register.
     * The lock argument is the shared constant word 0 at 0x00E6D92C
     * (SMD_ACQ_LOCK_DATA, declared in smd_internal.h and defined in
     * smd/smd_data.c), the same cell SMD_$BLT and SMD_$ASSOC pass.
     */
    *rec->ctrl_regs = SMD_$ACQ_DISPLAY((int16_t *)&SMD_ACQ_LOCK_DATA);

    /* 0x00E6F55C-0x00E6F560 */
    smd_$reset_unit_display(unit, (boolean)options);

    /* 0x00E6F566-0x00E6F570: Pascal boolean test - true is < 0 */
    if (options < 0) {
        /* The flags argument is the constant byte 0xFF at 0x00E6E458
         * (SMD_TRUE_DATA), i.e. "video on". */
        SMD_$VIDEO_CTL((uint8_t *)&SMD_TRUE_DATA, status_ret);
    }

    /* 0x00E6F576 */
    SMD_$REL_DISPLAY();
}
