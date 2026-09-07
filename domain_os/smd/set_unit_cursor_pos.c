/*
 * smd/set_unit_cursor_pos.c - SMD_$SET_UNIT_CURSOR_POS implementation
 *
 * Sets the cursor position for a specific display unit.
 *
 * Original address: 0x00E6E788
 */

#include "smd/smd_internal.h"
#include "tpad/tpad.h"

/*
 * Resolved (bead source-2c9v): these two by-reference arguments are code-region
 * constant cells, not variables holding their addresses.  The call site pushes
 *   0x00E6E7D6 "pea (A3)" (the pos argument), 0x00E6E7D2 "pea (-0x23a,PC)" -> 0x00E6E59A, 0x00E6E7CE "pea (-0x378,PC)" -> 0x00E6E458
 * ("pea (d,PC)" resolves to instruction + 2 + d), and SHOW_CURSOR dereferences
 * argument 2 as a word (0x00E6E1EA "move.w (A1),D6w") and argument 3 as a byte
 * (0x00E6E1EE "move.b (A2),D5b").  The cells hold 0xFFFF and 0xFF; the file
 * statics that used to live here held the *addresses* 0x00E6E59A / 0x00E6E458,
 * so SHOW_CURSOR read 0xE59A and 0x00 instead.  The named cells now live in
 * smd_data.c / smd_internal.h.
 */

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

    /*
     * Track unit changes.  0x00E6E7B8 "clr.l D0" / "move.w (A2),D0w" makes the
     * argument a *zero*-extended longword, while 0x00E6E7BC
     * "move.w (0x1d98,A5),D1w" / "ext.l D1" sign-extends the current unit, and
     * 0x00E6E7C2 "cmp.l D1,D0" compares the two longwords.  (Bead source-nuan:
     * (0x1d98,A5) is 0x00E84924 - the same word - so this really is
     * SMD_GLOBALS.default_unit.)
     */
    if ((uint32_t)(uint16_t)*unit != (uint32_t)(int32_t)SMD_GLOBALS.default_unit) {
        SMD_GLOBALS.unit_change_count++;   /* 0x00E6E7C6 addq.w #0x1,(0x1d9e,A5) */
    }
    SMD_GLOBALS.default_unit = (int16_t)*unit;  /* 0x00E6E7CA */

    /* Show cursor at new position */
    SHOW_CURSOR(pos, &SMD_MINUS_ONE_DATA, &SMD_TRUE_DATA);

    /* Synchronize with trackpad subsystem.  TPAD spells the same 32-bit
     * position out as the union smd_$pos_t {y at 0x00, x at 0x02}, which is
     * the m68k memory image of this packed value. */
    TPAD_$SET_UNIT_CURSOR((int16_t *)unit, (smd_$pos_t *)pos, status_ret);

    /* 00e6e7ec move.l (A3),(0xcc,A5): the whole packed longword. */
    __builtin_memcpy(&SMD_GLOBALS.saved_cursor_pos, pos, sizeof(uint32_t));
}
