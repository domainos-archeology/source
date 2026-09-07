/*
 * smd/inq_disp_type.c - SMD_$INQ_DISP_TYPE implementation
 *
 * Returns the display type code for a display unit.
 *
 * Original address: 0x00E6DE1C
 *
 * Assembly (verified with gsk analyze 0x00E6DE1C; the listing that used to be
 * quoted here was stale - the real code has no A5 setup, reads the unit once
 * into D2, and indexes the table with -0x60):
 *   00e6de1c    link.w A6,-0x4
 *   00e6de20    move.l D2,-(SP)
 *   00e6de22    movea.l (0x8,A6),A0           ; A0 = unit
 *   00e6de26    subq.l #0x2,SP                ; Pascal function result slot
 *   00e6de28    move.w (A0),D2w               ; D2 = *unit, read ONCE
 *   00e6de2a    move.w D2w,-(SP)
 *   00e6de2c    bsr.w 0x00e6d700              ; smd_$validate_unit
 *   00e6de30    addq.w #0x4,SP
 *   00e6de32    tst.b D0b
 *   00e6de34    bmi.b 0x00e6de3a              ; valid -> read the table
 *   00e6de36    clr.w D0w                     ; invalid -> return 0
 *   00e6de38    bra.b 0x00e6de50
 *   00e6de3a    move.w D2w,D0w
 *   00e6de3c    movea.l #0xe27376,A0
 *   00e6de42    ext.l D0                      ; signed index
 *   00e6de44    lsl.l #0x5,D0                 ; unit * 0x20
 *   00e6de46    move.l D0,D1
 *   00e6de48    add.l D1,D1                   ; unit * 0x40
 *   00e6de4a    add.l D1,D0                   ; unit * 0x60
 *   00e6de4c    move.w (-0x60,A0,D0*0x1),D0w  ; entry[unit-1].display_type
 *   00e6de50    move.l (-0x8,A6),D2
 *   00e6de54    unlk A6
 *   00e6de56    rts
 */

#include "smd/smd_internal.h"

/*
 * SMD_$INQ_DISP_TYPE - Inquire display type
 *
 * Parameters:
 *   unit - Pointer to display unit number
 *
 * Returns:
 *   Display type code, or 0 if the unit does not validate.
 *
 * Bead source-fqne: the explicit "-0x60" at 0x00E6DE4C makes the table 1-based
 * on the unit number, so this uses smd_$unit_info().  The file also carried a
 * private `smd_validate_unit` stub that guessed at units 0..3; the real
 * routine (smd/validate_unit.c, 0x00E6D700) accepts only unit 1 and is what
 * the `bsr.w 0x00e6d700` above calls.
 */
uint16_t SMD_$INQ_DISP_TYPE(uint16_t *unit)
{
    int16_t unit_num;

    /* 0x00E6DE28: the argument is fetched once and reused from D2. */
    unit_num = (int16_t)*unit;

    if (smd_$validate_unit((uint16_t)unit_num) >= 0) {
        return 0;   /* 0x00E6DE36 clr.w D0w */
    }

    return smd_$unit_info(unit_num)->display_type;
}
