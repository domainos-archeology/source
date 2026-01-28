/*
 * smd/validate_unit.c - smd_$validate_unit implementation
 *
 * Validates a display unit number. Only unit 1 is valid, and only
 * if the display is configured (display_type != 0).
 *
 * Original address: 0x00E6D700
 * Size: 54 bytes
 */

#include "smd/smd_internal.h"

/*
 * smd_$validate_unit - Validate display unit number
 *
 * Checks if the specified unit number is valid. Currently only unit 1
 * is supported, and it must have a non-zero display_type in the
 * display info table.
 *
 * Parameters:
 *   unit - Display unit number to validate
 *
 * Returns:
 *   0xFF (negative) if valid, 0 if invalid
 *
 * Original address: 0x00E6D700
 *
 * Assembly:
 *   00e6d700    link.w A6,-0x8
 *   00e6d704    move.l D2,-(SP)
 *   00e6d706    move.w (0x8,A6),D0w              ; D0 = unit
 *   00e6d70a    cmpi.w #0x1,D0w                  ; if unit != 1, invalid
 *   00e6d70e    bne.b 0x00e6d72c
 *   00e6d710    move.w D0w,D1w                   ; D1 = unit
 *   00e6d712    movea.l #0xe27376,A0             ; A0 = SMD_DISPLAY_INFO base
 *   00e6d718    ext.l D1                         ; sign-extend to long
 *   00e6d71a    lsl.l #0x5,D1                    ; D1 = unit * 32
 *   00e6d71c    move.l D1,D2                     ; D2 = unit * 32
 *   00e6d71e    add.l D2,D2                      ; D2 = unit * 64
 *   00e6d720    add.l D2,D1                      ; D1 = unit * 96 (0x60)
 *   00e6d722    tst.w (-0x60,A0,D1*0x1)          ; test display_type at info[unit-1]
 *   00e6d726    sne D1b                          ; D1 = (display_type != 0) ? 0xFF : 0
 *   00e6d728    move.b D1b,D0b                   ; return value
 *   00e6d72a    bra.b 0x00e6d72e
 *   00e6d72c    clr.b D0b                        ; return 0 (invalid)
 *   00e6d72e    move.l (-0xc,A6),D2
 *   00e6d732    unlk A6
 *   00e6d734    rts
 */
int8_t smd_$validate_unit(uint16_t unit)
{
    if (unit == 1) {
        /*
         * Unit 1 is valid if the display is configured.
         * Access pattern: SMD_DISPLAY_INFO[unit-1].display_type
         * Original assembly computes: base + unit*0x60 - 0x60
         */
        return (SMD_DISPLAY_INFO[unit - 1].display_type != 0) ? (int8_t)0xFF : 0;
    }

    /* All other unit numbers are invalid */
    return 0;
}
