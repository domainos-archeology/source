/*
 * smd/inq_disp_type.c - SMD_$INQ_DISP_TYPE implementation
 *
 * Returns the display type code for a display unit.
 *
 * Original address: 0x00E6DE1C
 *
 * Assembly:
 *   00e6de1c    link.w A6,0x0
 *   00e6de20    movem.l {  A5 A2},-(SP)
 *   00e6de24    lea (0xe82b8c).l,A5
 *   00e6de2a    movea.l (0x8,A6),A2               ; A2 = param unit
 *   00e6de2e    subq.l #0x2,SP
 *   00e6de30    move.w (A2),-(SP)                  ; push *unit
 *   00e6de32    bsr.w 0x00e6d700                   ; FUN_00e6d700 - validate unit
 *   00e6de36    addq.w #0x4,SP
 *   00e6de38    tst.b D0b
 *   00e6de3a    bpl.b 0x00e6de4e                   ; if invalid, return 0
 *   00e6de3c    move.w (A2),D0w                    ; D0 = *unit
 *   00e6de3e    movea.l #0xe27376,A0               ; A0 = display info table
 *   00e6de44    mulu.w #0x60,D0                    ; D0 = unit * 0x60
 *   00e6de48    move.w (0x0,A0,D0w*0x1),D0w        ; D0 = info[unit].display_type
 *   00e6de4c    bra.b 0x00e6de50
 *   00e6de4e    clr.w D0w                          ; return 0 (invalid)
 *   00e6de50    movem.l (-0x8,A6),{  A2 A5}
 *   00e6de54    unlk A6
 *   00e6de56    rts
 */

#include "smd/smd_internal.h"

/* Forward declaration of internal validation function */
static int8_t smd_validate_unit(uint16_t unit);

/*
 * SMD_$INQ_DISP_TYPE - Inquire display type
 *
 * Returns the display type code for the specified unit.
 *
 * Parameters:
 *   unit - Pointer to display unit number
 *
 * Returns:
 *   Display type code (1-11), or 0 if invalid unit
 */
uint16_t SMD_$INQ_DISP_TYPE(uint16_t *unit)
{
    /* Validate the unit number */
    if (smd_validate_unit(*unit) >= 0) {
        /* Invalid unit - return 0 */
        return 0;
    }

    /* Return display type from info table */
    return SMD_DISPLAY_INFO[*unit].display_type;
}

/*
 * smd_validate_unit - Validate display unit number
 *
 * Internal helper to check if a unit number is valid.
 *
 * Parameters:
 *   unit - Display unit number to validate
 *
 * Returns:
 *   Negative value if valid, non-negative if invalid
 *
 * Note: The negative return for valid follows the original code's
 * convention where the validation function sets D0 negative on success.
 * This is likely because it returns -1 (0xFF) for true in Domain/OS style.
 *
 * Original address: 0x00E6D700 (FUN_00e6d700)
 */
static int8_t smd_validate_unit(uint16_t unit)
{
    /*
     * TODO: Full implementation requires understanding FUN_00e6d700.
     * For now, assume units 0-3 are potentially valid.
     * The original likely checks:
     * 1. Unit < max_units
     * 2. Unit has valid hardware present
     */
    if (unit < SMD_MAX_DISPLAY_UNITS) {
        /* Check if display info exists for this unit */
        if (SMD_DISPLAY_INFO[unit].display_type != 0) {
            return -1;  /* Valid (negative = true in Domain/OS) */
        }
    }
    return 0;  /* Invalid */
}
