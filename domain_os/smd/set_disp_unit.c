/*
 * smd/set_disp_unit.c - SMD_$SET_DISP_UNIT implementation
 *
 * Set the current display unit for a process.
 *
 * Original address: 0x00E6F7D4
 *
 * This function sets which display unit the current process uses for
 * subsequent SMD operations. It validates the unit number before setting.
 *
 * Assembly:
 *   00e6f7d4    link.w A6,0x0
 *   00e6f7d8    movem.l {  A5 A3 A2},-(SP)
 *   00e6f7dc    lea (0xe82b8c).l,A5           ; A5 = globals base
 *   00e6f7e2    movea.l (0x8,A6),A2           ; A2 = unit param
 *   00e6f7e6    subq.l #0x2,SP
 *   00e6f7e8    movea.l (0xc,A6),A3           ; A3 = status_ret
 *   00e6f7ec    move.w (A2),-(SP)             ; push *unit
 *   00e6f7ee    bsr.w 0x00e6d700              ; smd_$validate_unit
 *   00e6f7f2    addq.w #0x4,SP
 *   00e6f7f4    tst.b D0b
 *   00e6f7f6    bmi.b 0x00e6f800              ; if valid, jump to set
 *   00e6f7f8    move.l #0x130001,(A3)         ; *status_ret = invalid_unit
 *   00e6f7fe    bra.b 0x00e6f80e
 *   00e6f800    move.w (0x00e2060a).l,D0w     ; D0 = PROC1_$AS_ID
 *   00e6f806    add.w D0w,D0w                 ; D0 *= 2 (word offset)
 *   00e6f808    move.w (A2),(0x48,A5,D0w*0x1) ; asid_to_unit[ASID] = *unit
 *   00e6f80c    clr.l (A3)                    ; *status_ret = 0 (ok)
 *   00e6f80e    movem.l (-0xc,A6),{  A2 A3 A5}
 *   00e6f814    unlk A6
 *   00e6f816    rts
 */

#include "smd/smd_internal.h"

/* Forward declaration for validation helper */
extern int8_t smd_$validate_unit(uint16_t unit);

/*
 * SMD_$SET_DISP_UNIT - Set display unit for current process
 *
 * Sets the display unit mapping for the current process's ASID.
 * This determines which physical display the process uses.
 *
 * Parameters:
 *   unit       - Pointer to display unit number
 *   status_ret - Status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_unit_number - Invalid unit number
 */
void SMD_$SET_DISP_UNIT(uint16_t *unit, status_$t *status_ret)
{
    int8_t valid;
    uint16_t asid_offset;

    /* Validate the unit number */
    valid = smd_$validate_unit(*unit);

    if (valid >= 0) {
        /* Invalid unit number */
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Set the ASID-to-unit mapping for current process */
    asid_offset = PROC1_$AS_ID;
    SMD_GLOBALS.asid_to_unit[asid_offset] = *unit;

    *status_ret = status_$ok;
}
