/*
 * smd/unblank.c - SMD_$UNBLANK implementation
 *
 * Restores display output after screen blanking.
 *
 * Original address: 0x00E6EFB4
 *
 * Assembly:
 *   00e6efb4    link.w A6,-0x4
 *   00e6efb8    pea (A5)
 *   00e6efba    lea (0xe82b8c).l,A5
 *   00e6efc0    move.l (0x00e2b0d4).l,(0xc8,A5)    ; blank_time = TIME_$CLOCKH
 *   00e6efc8    tst.b (0xdd,A5)                     ; if (blank_enabled < 0)
 *   00e6efcc    bpl.b 0x00e6efe8
 *   00e6efce    move.w (0x00e2060a).l,D0w           ; D0 = PROC1_$AS_ID
 *   00e6efd4    add.w D0w,D0w                       ; D0 *= 2 (index into word array)
 *   00e6efd6    move.w (0x1d98,A5),(0x48,A5,D0w*0x1) ; asid_to_unit[asid] = default_unit
 *   00e6efdc    pea (-0x4,A6)                       ; status_ret
 *   00e6efe0    pea (-0xb8a,PC)                     ; &SMD_VIDEO_ENABLE constant
 *   00e6efe4    bsr.w 0x00e6f838                    ; SMD_$VIDEO_CTL
 *   00e6efe8    movea.l (-0x8,A6),A5
 *   00e6efec    unlk A6
 *   00e6efee    rts
 */

#include "smd/smd_internal.h"
#include "time/time.h"

/*
 * SMD_$UNBLANK - Unblank the display
 *
 * Called when user input is detected on a blanked display.
 * Updates the blank timestamp and, if the display was blanked,
 * re-enables video output and restores the ASID-to-unit mapping.
 */
void SMD_$UNBLANK(void)
{
    status_$t status;
    uint8_t enable_flag = SMD_VIDEO_ENABLE;

    /* Update blank timestamp with current time */
    SMD_GLOBALS.blank_time = TIME_$CLOCKH;

    /* Check if display is currently blanked (negative = blanked) */
    if (SMD_GLOBALS.blank_enabled < 0) {
        /* Restore the ASID-to-unit mapping for current process */
        SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = SMD_GLOBALS.default_unit;

        /* Re-enable video output */
        SMD_$VIDEO_CTL(&enable_flag, &status);
    }
}
