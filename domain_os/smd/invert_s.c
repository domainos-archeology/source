/*
 * smd/invert_s.c - SMD_$INVERT_S implementation
 *
 * User-facing function to invert the display. Validates that the
 * calling process has an associated display, acquires the display
 * lock, and calls the low-level invert function.
 *
 * Original address: 0x00E6DDA6
 */

#include "smd/smd_internal.h"

/* Lock data for display acquisition */
static const uint32_t invert_s_lock_data = 0x00E6D92C;

/*
 * SMD_$INVERT_S - Invert display (user-callable)
 *
 * Inverts the display associated with the calling process.
 * Acquires the display lock for exclusive access during the operation.
 *
 * Parameters:
 *   status_ret - Status return
 *
 * On success:
 *   Display is inverted, status_ret is status_$ok
 *
 * On failure:
 *   status_ret is status_$display_invalid_use_of_driver_procedure
 *   if no display is associated with the calling process
 *
 * Original address: 0x00E6DDA6
 *
 * Assembly:
 *   00e6dda6    link.w A6,-0x8
 *   00e6ddaa    movem.l {  A5 A2 D2},-(SP)
 *   00e6ddae    lea (0xe82b8c).l,A5        ; A5 = SMD_GLOBALS base
 *   00e6ddb4    movea.l (0x8,A6),A0        ; A0 = status_ret
 *   00e6ddb8    move.l #0x130004,(A0)      ; *status_ret = invalid_use
 *   00e6ddbe    move.w (0x00e2060a).l,D0w  ; D0 = PROC1_$AS_ID
 *   00e6ddc4    add.w D0w,D0w              ; D0 *= 2
 *   00e6ddc6    move.w (0x48,A5,D0w*0x1),D2w ; D2 = unit number
 *   00e6ddca    beq.b 0x00e6de12           ; if 0, exit (error)
 *   00e6ddcc    move.w D2w,D0w             ; D0 = unit number
 *   00e6ddce    movea.l #0xe27376,A1       ; A1 = SMD_DISPLAY_INFO base
 *   00e6ddd4    ext.l D0                   ; sign extend
 *   00e6ddd6    lsl.l #0x5,D0              ; D0 *= 32
 *   00e6ddd8    move.l D0,D1               ; D1 = D0
 *   00e6ddda    add.l D1,D1                ; D1 *= 2
 *   00e6dddc    add.l D1,D0                ; D0 += D1 (D0 *= 3, so D0 = unit * 96)
 *   00e6ddde    lea (0x0,A1,D0*0x1),A2     ; A2 = &SMD_DISPLAY_INFO[unit]
 *   00e6dde2    clr.l (A0)                 ; *status_ret = 0
 *   00e6dde4    pea (-0x4ba,PC)            ; push lock_data
 *   00e6dde8    bsr.w 0x00e6eb42           ; ACQ_DISPLAY
 *   00e6ddec    addq.w #0x4,SP
 *   00e6ddee    pea (-0x60,A2)             ; push display_info - 0x60
 *   00e6ddf2    move.w D2w,D1w             ; D1 = unit
 *   00e6ddf4    movea.l #0xe2e3fc,A0       ; A0 = SMD_DISPLAY_UNITS base
 *   00e6ddfa    muls.w #0x10c,D1           ; D1 = unit * 0x10C
 *   00e6ddfe    lea (0x0,A0,D1*0x1),A1     ; A1 = &unit[D2]
 *   00e6de02    move.l (0x14,A1),-(SP)     ; push unit.field_14 (display base)
 *   00e6de06    jsr 0x00e70376.l           ; INVERT_DISP
 *   00e6de0c    addq.w #0x8,SP
 *   00e6de0e    bsr.w 0x00e6ec10           ; REL_DISPLAY
 *   00e6de12    movem.l (-0x14,A6),{  D2 A2 A5}
 *   00e6de18    unlk A6
 *   00e6de1a    rts
 */
void SMD_$INVERT_S(status_$t *status_ret)
{
    uint16_t asid;
    uint16_t unit_num;
    smd_display_unit_t *unit;
    smd_display_info_t *info;

    /* Default to error status */
    *status_ret = status_$display_invalid_use_of_driver_procedure;

    /* Get current process's address space ID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit_num = SMD_GLOBALS.asid_to_unit[asid];

    if (unit_num == 0) {
        /* No display associated with this process */
        return;
    }

    /* Clear status - we have a valid display */
    *status_ret = status_$ok;

    /* Get unit and info pointers */
    unit = &SMD_DISPLAY_UNITS[unit_num];
    info = &SMD_DISPLAY_INFO[unit_num];

    /* Acquire display for exclusive access */
    SMD_$ACQ_DISPLAY((void *)&invert_s_lock_data);

    /*
     * Call INVERT_DISP with:
     *   - display_base from unit.field_14
     *   - display_info offset by -0x60 from computed address
     *
     * The -0x60 offset in the original code adjusts for the
     * way the info table is indexed. Since info = &table[unit],
     * info - 0x60 would be &table[unit-1] (each entry is 0x60 bytes).
     * This suggests the display_info parameter is used to access
     * data from the "previous" entry, likely for some hardware
     * configuration purpose.
     */
    SMD_$INVERT_DISP(unit->field_14, info - 1);

    /* Release display lock */
    SMD_$REL_DISPLAY();
}
