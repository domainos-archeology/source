/*
 * smd/video_ctl.c - SMD_$VIDEO_CTL implementation
 *
 * Controls video output enable/disable.
 *
 * Original address: 0x00E6F838
 *
 * Assembly:
 *   00e6f838    link.w A6,-0xc
 *   00e6f83c    movem.l {  A5 A3 A2 D2},-(SP)
 *   00e6f840    lea (0xe82b8c).l,A5
 *   00e6f846    movea.l (0x8,A6),A2               ; A2 = flags param
 *   00e6f84a    move.w (0x00e2060a).l,D0w         ; D0 = PROC1_$AS_ID
 *   00e6f850    movea.l (0xc,A6),A0               ; A0 = status_ret
 *   00e6f854    add.w D0w,D0w                     ; D0 *= 2
 *   00e6f856    move.w (0x48,A5,D0w*0x1),D2w      ; D2 = asid_to_unit[asid]
 *   00e6f85a    bne.b 0x00e6f864                  ; if unit != 0, continue
 *   00e6f85c    move.l #0x130004,(A0)             ; status = invalid_use
 *   00e6f862    bra.b 0x00e6f8c6
 *   00e6f864    clr.l (A0)                        ; *status_ret = 0
 *   ; ... continues with video enable/disable logic
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
    uint16_t unit;
    smd_display_unit_t *disp_unit;
    smd_display_hw_t *hw;
    uint16_t video_flags;
    uint16_t acq_result;

    /* Get display unit for current process */
    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        /* No display associated with this process */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    *status_ret = status_$ok;

    /* Get unit and hardware pointers */
    disp_unit = smd_get_unit(unit);
    hw = disp_unit->hw;

    /* Update video flags - bit 0 controls video enable */
    video_flags = hw->video_flags & 0xFFFE;  /* Clear enable bit */
    video_flags |= ((*flags) >> 7) & 0x01;   /* Set from input bit 7 */
    hw->video_flags = video_flags;

    /* Acquire display lock for synchronization */
    /* Note: The original uses a static lock data address */
    acq_result = SMD_$ACQ_DISPLAY(NULL);  /* TODO: proper lock data */

    /* Write the new video flags to hardware event count */
    /* The event count at offset 0x8 from unit stores video state */
    /* TODO: This needs proper hardware register access */

    /* Release display lock */
    SMD_$REL_DISPLAY();

    /* Update blanking state if this is the default display */
    if (unit == SMD_GLOBALS.default_unit) {
        SMD_GLOBALS.blank_enabled = ~(*flags);  /* Inverted logic */
        if ((int8_t)*flags < 0) {
            /* Video being enabled - update blank time */
            SMD_GLOBALS.blank_time = TIME_$CLOCKH;
        }
    }
}
