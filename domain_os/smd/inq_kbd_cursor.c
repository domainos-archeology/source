/*
 * smd/inq_kbd_cursor.c - SMD_$INQ_KBD_CURSOR implementation
 *
 * Reports the cursor position and visibility flag recorded for the current
 * default display unit.
 *
 * Original address: 0x00E6E0D4
 */

#include "smd/smd_internal.h"

/*
 * SMD_$INQ_KBD_CURSOR - Inquire keyboard cursor position
 *
 * Parameters:
 *   pos        - receives the packed cursor position of the default unit
 *   status_ret - always set to status_$ok (0x00E6E0E4 "clr.l (A0)")
 *
 * Returns:
 *   The unit's cursor-visible byte when the default unit validates, otherwise
 *   smd_$validate_unit's own result (0x00), because the invalid path branches
 *   straight to the epilogue with D0.b still holding that result.
 *
 * Assembly (verified with gsk analyze 0x00E6E0D4):
 *   00e6e0d4    link.w A6,-0x8
 *   00e6e0d8    pea (A5)
 *   00e6e0da    lea (0xe82b8c).l,A5           ; A5 = SMD_GLOBALS
 *   00e6e0e0    movea.l (0xc,A6),A0           ; A0 = status_ret
 *   00e6e0e4    clr.l (A0)                    ; *status_ret = status_$ok
 *   00e6e0e6    subq.l #0x2,SP                ; Pascal function result slot
 *   00e6e0e8    move.w (0x1d98,A5),-(SP)      ; push SMD_GLOBALS.default_unit
 *   00e6e0ec    bsr.w 0x00e6d700              ; smd_$validate_unit
 *   00e6e0f0    addq.w #0x4,SP
 *   00e6e0f2    tst.b D0b
 *   00e6e0f4    bpl.b 0x00e6e11a              ; invalid -> return D0.b (= 0)
 *   00e6e0f6    move.w (0x1d98,A5),D0w
 *   00e6e0fa    movea.l #0xe27376,A0          ; display info/hw table base
 *   00e6e100    ext.l D0                      ; signed
 *   00e6e102    lsl.l #0x5,D0                 ; unit * 0x20
 *   00e6e104    move.l D0,D1
 *   00e6e106    add.l D1,D1                   ; unit * 0x40
 *   00e6e108    add.l D1,D0                   ; unit * 0x60
 *   00e6e10a    lea (0x0,A0,D0*0x1),A0        ; A0 = base + unit*0x60
 *   00e6e10e    movea.l (0x8,A6),A1           ; A1 = pos
 *   00e6e112    move.b (-0x28,A0),D0b         ; D0.b = entry[unit-1] + 0x38
 *   00e6e116    move.l (-0x2e,A0),(A1)        ; *pos  = entry[unit-1] + 0x32
 *   00e6e11a    movea.l (-0xc,A6),A5
 *   00e6e11e    unlk A6
 *   00e6e120    rts
 *
 * Bead source-fqne: A0 is base + unit*0x60 and every field displacement is
 * negative, so the entry actually addressed is base + unit*0x60 - 0x60, i.e.
 * the table is 1-based on the unit number.  -0x2E from A0 is entry offset
 * 0x32 (cursor_pos) and -0x28 is entry offset 0x38 (cursor_visible), which is
 * exactly what smd_$reset_display_globals clears (0x00E6D80E / 0x00E6D816).
 * Use smd_$unit_info(unit) rather than open-coded byte arithmetic.
 */
uint8_t SMD_$INQ_KBD_CURSOR(smd_cursor_pos_t *pos, status_$t *status_ret)
{
    int8_t valid;
    uint8_t result;
    smd_display_info_t *info;

    *status_ret = status_$ok;

    valid = smd_$validate_unit((uint16_t)SMD_GLOBALS.default_unit);
    result = (uint8_t)valid;

    if (valid < 0) {
        info = smd_$unit_info(SMD_GLOBALS.default_unit);

        /* 0x00E6E112 first, then 0x00E6E116 - order kept for fidelity. */
        result = (uint8_t)info->cursor_visible;
        *pos = info->cursor_pos;
    }

    return result;
}
