/*
 * smd/video_ctl.c - SMD_$VIDEO_CTL implementation
 *
 * Controls video output enable/disable.
 *
 * Original address: 0x00E6F838
 *
 * Assembly (every instruction of the function is accounted for below):
 *   00e6f838    link.w A6,-0xc
 *   00e6f83c    movem.l {  A5 A3 A2 D2},-(SP)
 *   00e6f840    lea (0xe82b8c).l,A5               ; A5 = &SMD_GLOBALS
 *   00e6f846    movea.l (0x8,A6),A2               ; A2 = flags param
 *   00e6f84a    move.w (0x00e2060a).l,D0w         ; D0 = PROC1_$AS_ID
 *   00e6f850    movea.l (0xc,A6),A0               ; A0 = status_ret
 *   00e6f854    add.w D0w,D0w                     ; D0 *= 2
 *   00e6f856    move.w (0x48,A5,D0w*0x1),D2w      ; D2 = asid_to_unit[asid]
 *   00e6f85a    bne.b 0x00e6f864                  ; if unit != 0, continue
 *   00e6f85c    move.l #0x130004,(A0)             ; status = invalid_use
 *   00e6f862    bra.b 0x00e6f8c6
 *   00e6f864    clr.l (A0)                        ; *status_ret = 0
 *   00e6f866    move.w D2w,D0w
 *   00e6f868    movea.l #0xe2e3fc,A1
 *   00e6f86e    muls.w #0x10c,D0                  ; signed unit * 0x10C
 *   00e6f872    lea (0x0,A1,D0*0x1),A3            ; A3 = biased unit record
 *   00e6f876    movea.l (-0xf4,A3),A0             ; A0 = rec->hw
 *   00e6f87a    move.w (0x22,A0),(-0x2,A6)        ; local = hw->video_flags
 *   00e6f880    move.b (A2),D0b
 *   00e6f882    lsr.b #0x7,D0b                    ; D0 = *flags >> 7 (0 or 1)
 *   00e6f884    andi.b #-0x2,(-0x1,A6)            ; local &= 0xFFFE
 *   00e6f88a    or.b D0b,(-0x1,A6)                ; local |= bit
 *   00e6f88e    move.w (-0x2,A6),(0x22,A0)        ; hw->video_flags = local
 *   00e6f894    pea (-0x1f6a,PC)                  ; &SMD_ACQ_LOCK_DATA
 *                                                 ; (0x00e6f896-0x1f6a = 0x00e6d92c)
 *   00e6f898    bsr.w 0x00e6eb42                  ; SMD_$ACQ_DISPLAY
 *   00e6f89c    addq.w #0x4,SP
 *   00e6f89e    move.w D0w,(-0x2,A6)              ; local = acquire result
 *   00e6f8a2    movea.l (0x8,A3),A0               ; A0 = rec->ctrl_regs (+0xFC)
 *   00e6f8a6    move.w D0w,(A0)                   ; ctrl_regs[0] = result
 *   00e6f8a8    bsr.w 0x00e6ec10                  ; SMD_$REL_DISPLAY
 *   00e6f8ac    cmp.w (0x1d98,A5),D2w             ; unit == default_unit?
 *   00e6f8b0    bne.b 0x00e6f8c6
 *   00e6f8b2    move.b (A2),D0b
 *   00e6f8b4    not.b D0b
 *   00e6f8b6    move.b D0b,(0xdd,A5)              ; blank_pending = ~*flags
 *   00e6f8ba    tst.b (A2)
 *   00e6f8bc    bpl.b 0x00e6f8c6
 *   00e6f8be    move.l (0x00e2b0d4).l,(0xc8,A5)   ; blank_time = TIME_$CLOCKH
 *   00e6f8c6    movem.l (-0x1c,A6),{  D2 A2 A3 A5}
 *   00e6f8cc    unlk A6
 *   00e6f8ce    rts
 */

#include "smd/smd_internal.h"
#include "time/time.h"

/*
 * SMD_$VIDEO_CTL - Video control
 *
 * Enables or disables video output for the current process's display.
 *
 * Parameters:
 *   flags - Pointer to video control flags
 *           bit 7: 1 = enable video, 0 = disable video
 *   status_ret - Status return
 */
void SMD_$VIDEO_CTL(uint8_t *flags, status_$t *status_ret)
{
    int16_t unit;
    smd_display_unit_t *rec;
    smd_display_hw_t *hw;
    uint16_t video_flags;

    /* Get display unit for current process (0x00e6f856) */
    unit = (int16_t)SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        /* No display associated with this process (0x00e6f85c) */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    *status_ret = status_$ok; /* 0x00e6f864 clr.l (A0) */

    /* 0x00e6f868-0x00e6f876: A3 = 0xE2E3FC + unit*0x10C, hw at (-0xF4,A3) */
    rec = smd_$unit_rec(unit);
    hw = rec->hw;

    /* Update video flags - bit 0 controls video enable (0x00e6f87a-0x00e6f88e).
     * The original reads the whole word into a local and rewrites the whole
     * word, but only touches the low byte in between. */
    video_flags = (uint16_t)(hw->video_flags & 0xFFFEu);
    video_flags = (uint16_t)(video_flags | ((uint16_t)(*flags >> 7) & 0x01u));
    hw->video_flags = video_flags;

    /* Acquire the display, hand the returned control word to the display
     * controller registers, then release (0x00e6f894-0x00e6f8a8). */
    rec->ctrl_regs[0] = SMD_$ACQ_DISPLAY((int16_t *)&SMD_ACQ_LOCK_DATA);
    SMD_$REL_DISPLAY();

    /* Update blanking state if this is the default display (0x00e6f8ac) */
    if (unit == SMD_GLOBALS.default_unit) {
        /* 0x00e6f8b2-0x00e6f8b6: blank_pending (0xDD), not blank_enabled */
        SMD_GLOBALS.blank_pending = (boolean)~(*flags);
        /* 0x00e6f8ba tst.b / bpl: signed test of the caller's byte */
        if ((int8_t)*flags < 0) {
            /* Video being enabled - restart the blanking timer */
            SMD_GLOBALS.blank_time = TIME_$CLOCKH;
        }
    }
}
