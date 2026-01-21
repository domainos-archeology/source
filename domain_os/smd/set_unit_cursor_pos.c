/*
 * smd/set_unit_cursor_pos.c - SMD_$SET_UNIT_CURSOR_POS implementation
 *
 * Sets the cursor position for a specific display unit.
 *
 * Original address: 0x00E6E788
 */

#include "smd/smd_internal.h"
#include "tpad/tpad.h"

/* Lock data addresses from original code */
static const uint32_t cursor_show_lock_data_1 = 0x00E6E59A;
static const uint32_t cursor_show_lock_data_2 = 0x00E6E458;

/*
 * SMD_$SET_UNIT_CURSOR_POS - Set cursor position for a specific unit
 *
 * Sets the cursor position for the specified display unit. Updates
 * cursor change tracking and calls the trackpad/touchpad subsystem
 * to synchronize cursor position.
 *
 * Parameters:
 *   unit       - Pointer to display unit number
 *   pos        - Pointer to new cursor position (x, y)
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E788
 *
 * Assembly:
 *   00e6e788    link.w A6,0x0
 *   00e6e78c    movem.l {  A5 A4 A3 A2},-(SP)
 *   00e6e790    lea (0xe82b8c).l,A5
 *   00e6e796    movea.l (0x8,A6),A2           ; A2 = unit
 *   00e6e79a    movea.l (0xc,A6),A3           ; A3 = pos
 *   00e6e79e    subq.l #0x2,SP
 *   00e6e7a0    movea.l (0x10,A6),A4          ; A4 = status_ret
 *   00e6e7a4    move.w (A2),-(SP)
 *   00e6e7a6    bsr.w 0x00e6d700              ; validate_unit(*unit)
 *   00e6e7aa    addq.w #0x4,SP
 *   00e6e7ac    tst.b D0b
 *   00e6e7ae    bmi.b 0x00e6e7b8              ; if valid, continue
 *   00e6e7b0    move.l #0x130001,(A4)         ; invalid unit error
 *   00e6e7b6    bra.b 0x00e6e7f0
 *   00e6e7b8    clr.l D0
 *   00e6e7ba    move.w (A2),D0w
 *   00e6e7bc    move.w (0x1d98,A5),D1w        ; default_unit
 *   00e6e7c0    ext.l D1
 *   00e6e7c2    cmp.l D1,D0
 *   00e6e7c4    beq.b 0x00e6e7ca              ; if same unit, skip counter
 *   00e6e7c6    addq.w #0x1,(0x1d9e,A5)       ; unit_change_count++
 *   00e6e7ca    move.w (A2),(0x1d98,A5)       ; default_unit = *unit
 *   00e6e7ce    pea (-0x378,PC)               ; lock_data_2
 *   00e6e7d2    pea (-0x23a,PC)               ; lock_data_1
 *   00e6e7d6    pea (A3)                      ; pos
 *   00e6e7d8    bsr.w 0x00e6e1cc              ; SHOW_CURSOR
 *   00e6e7dc    lea (0xc,SP),SP
 *   00e6e7e0    pea (A4)                      ; status_ret
 *   00e6e7e2    pea (A3)                      ; pos
 *   00e6e7e4    pea (A2)                      ; unit
 *   00e6e7e6    jsr 0x00e698c2.l              ; TPAD_$SET_UNIT_CURSOR
 *   00e6e7ec    move.l (A3),(0xcc,A5)         ; saved_cursor_pos = *pos
 *   00e6e7f0    movem.l (-0x10,A6),{  A2 A3 A4 A5}
 *   00e6e7f6    unlk A6
 *   00e6e7f8    rts
 */
void SMD_$SET_UNIT_CURSOR_POS(uint16_t *unit, smd_cursor_pos_t *pos, status_$t *status_ret)
{
    int8_t valid;

    valid = smd_$validate_unit(*unit);
    if (valid >= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Track unit changes */
    if ((uint32_t)*unit != (int32_t)(int16_t)SMD_DEFAULT_DISPLAY_UNIT) {
        SMD_GLOBALS.unit_change_count++;
    }
    SMD_DEFAULT_DISPLAY_UNIT = *unit;

    /* Show cursor at new position */
    SHOW_CURSOR((uint32_t *)pos, (int16_t *)&cursor_show_lock_data_1,
                (int8_t *)&cursor_show_lock_data_2);

    /* Synchronize with trackpad subsystem */
    TPAD_$SET_UNIT_CURSOR(unit, pos, status_ret);

    /* Save current cursor position */
    SMD_GLOBALS.saved_cursor_pos = *pos;
}
