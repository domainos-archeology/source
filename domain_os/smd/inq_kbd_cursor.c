/*
 * smd/inq_kbd_cursor.c - SMD_$INQ_KBD_CURSOR implementation
 *
 * Inquires the current keyboard cursor position and type.
 *
 * Original address: 0x00E6E0D4
 */

#include "smd/smd_internal.h"

/*
 * SMD_$INQ_KBD_CURSOR - Inquire keyboard cursor position
 *
 * Returns the current keyboard cursor position and cursor type
 * for the default display unit.
 *
 * Parameters:
 *   pos        - Pointer to receive cursor position (x, y)
 *   status_ret - Pointer to status return
 *
 * Returns:
 *   Function returns cursor type in D0 (via return value)
 *   pos is filled with current cursor position
 *   status_ret is set to status_$ok
 *
 * Original address: 0x00E6E0D4
 *
 * Assembly:
 *   00e6e0d4    link.w A6,-0x8
 *   00e6e0d8    pea (A5)
 *   00e6e0da    lea (0xe82b8c).l,A5
 *   00e6e0e0    movea.l (0xc,A6),A0
 *   00e6e0e4    clr.l (A0)                    ; *status_ret = 0
 *   00e6e0e6    subq.l #0x2,SP
 *   00e6e0e8    move.w (0x1d98,A5),-(SP)      ; push default_unit
 *   00e6e0ec    bsr.w 0x00e6d700              ; smd_$validate_unit
 *   00e6e0f0    addq.w #0x4,SP
 *   00e6e0f2    tst.b D0b
 *   00e6e0f4    bpl.b 0x00e6e11a              ; if invalid, exit
 *   00e6e0f6    move.w (0x1d98,A5),D0w        ; D0 = default_unit
 *   00e6e0fa    movea.l #0xe27376,A0          ; display info base
 *   00e6e100    ext.l D0
 *   00e6e102    lsl.l #0x5,D0                 ; unit * 32
 *   00e6e104    move.l D0,D1
 *   00e6e106    add.l D1,D1                   ; unit * 64
 *   00e6e108    add.l D1,D0                   ; unit * 96 (0x60)
 *   00e6e10a    lea (0x0,A0,D0*0x1),A0        ; A0 = &display_info[unit]
 *   00e6e10e    movea.l (0x8,A6),A1           ; A1 = pos
 *   00e6e112    move.b (-0x28,A0),D0b         ; D0 = cursor_type (offset 0x4E from base)
 *   00e6e116    move.l (-0x2e,A0),(A1)        ; *pos = cursor_pos (offset 0x48 from base)
 *   00e6e11a    movea.l (-0xc,A6),A5
 *   00e6e11e    unlk A6
 *   00e6e120    rts
 */
uint8_t SMD_$INQ_KBD_CURSOR(smd_cursor_pos_t *pos, status_$t *status_ret)
{
    uint8_t result;
    int32_t offset;
    smd_display_info_t *info;

    *status_ret = status_$ok;

    result = smd_$validate_unit(SMD_DEFAULT_DISPLAY_UNIT);
    if ((int8_t)result < 0) {
        /* Unit is valid - calculate display info offset */
        offset = (int32_t)(int16_t)SMD_DEFAULT_DISPLAY_UNIT * SMD_DISPLAY_INFO_SIZE;

        /* Get pointer to display info - using byte math like original */
        info = (smd_display_info_t *)((uint8_t *)&SMD_DISPLAY_INFO[0] + offset);

        /* Return cursor position (offset 0x48 from display_info base = 0x32 from computed) */
        *pos = *(smd_cursor_pos_t *)((uint8_t *)info - 0x2e + SMD_DISPLAY_INFO_SIZE);

        /* Return cursor type (offset 0x4E from display_info base = 0x38 from computed - 0x60) */
        result = *((uint8_t *)info - 0x28 + SMD_DISPLAY_INFO_SIZE);
    }

    return result;
}
