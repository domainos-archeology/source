/*
 * smd/inq_disp_uid.c - SMD_$INQ_DISP_UID implementation
 *
 * Inquires the UID for a display unit.
 *
 * Original address: 0x00E70062
 *
 * Assembly:
 *   link.w A6,0x0
 *   movem.l {A5 A2},-(SP)
 *   lea (0xe82b8c).l,A5      ; SMD_GLOBALS base
 *   movea.l (0x10,A6),A2     ; status_ret
 *   movea.l #0xe1737c,A0     ; UID_$NIL address
 *   movea.l (0xc,A6),A1      ; uid output buffer
 *   move.l (A0)+,(A1)        ; uid->high = UID_$NIL.high
 *   move.l (A0)+,(0x4,A1)    ; uid->low = UID_$NIL.low
 *   subq.l #0x2,SP
 *   movea.l (0x8,A6),A0      ; unit ptr
 *   move.w (A0),-(SP)        ; push *unit
 *   bsr.w FUN_00e6d700       ; validate unit
 *   addq.w #0x4,SP
 *   tst.b D0b
 *   bmi.b valid              ; if negative, unit is valid
 *   move.l #0x130001,(A2)    ; invalid unit number error
 *   bra.b done
 * valid:
 *   clr.l (A2)               ; status = ok
 * done:
 *   movem.l (-0x8,A6),{A2 A5}
 *   unlk A6
 *   rts
 */

#include "smd/smd_internal.h"
#include "uid/uid.h"

/*
 * SMD_$INQ_DISP_UID - Inquire display UID
 *
 * Returns the UID for a display unit. Currently returns UID_$NIL
 * for all valid units since display UIDs are not implemented.
 *
 * Parameters:
 *   unit       - Pointer to display unit number
 *   uid        - Output: receives the display UID (always UID_$NIL)
 *   status_ret - Output: status return
 *
 * Returns:
 *   status_$ok if unit is valid
 *   status_$display_invalid_unit_number if unit is invalid
 */
void SMD_$INQ_DISP_UID(uint16_t *unit, uid_$t *uid, status_$t *status_ret)
{
    int8_t valid;

    /* Copy UID_$NIL to output buffer */
    uid->high = UID_$NIL.high;
    uid->low = UID_$NIL.low;

    /* Validate the unit number */
    valid = smd_$validate_unit(*unit);

    if (valid < 0) {
        /* Unit is valid */
        *status_ret = status_$ok;
    } else {
        /* Invalid unit number */
        *status_ret = status_$display_invalid_unit_number;
    }
}
