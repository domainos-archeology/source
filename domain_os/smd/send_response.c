/*
 * smd/send_response.c - SMD_$SEND_RESPONSE implementation
 *
 * Records the screen manager's answer to a pending borrow request and wakes
 * the waiting process.
 *
 * Original address: 0x00E6F4BE
 *
 * Assembly (verified with gsk analyze 0x00E6F4BE):
 *   00e6f4be    link.w A6,-0xc
 *   00e6f4c2    movem.l {  A5 A3 A2 D2},-(SP)
 *   00e6f4c6    lea (0xe82b8c).l,A5           ; A5 = SMD_GLOBALS
 *   00e6f4cc    move.w (0x00e2060a).l,D0w     ; PROC1_$AS_ID
 *   00e6f4d2    add.w D0w,D0w
 *   00e6f4d4    move.w (0x48,A5,D0w*0x1),D0w  ; unit = asid_to_unit[asid]
 *   00e6f4d8    beq.b 0x00e6f50a              ; no unit -> nothing to do
 *   00e6f4da    clr.l D1
 *   00e6f4dc    movea.l #0xe27376,A0
 *   00e6f4e2    move.w D0w,D1w                ; zero-extended unit
 *   00e6f4e4    lsl.l #0x5,D1
 *   00e6f4e6    move.l D1,D2
 *   00e6f4e8    add.l D2,D2
 *   00e6f4ea    add.l D2,D1                   ; D1 = unit * 0x60
 *   00e6f4ec    lea (0x0,A0,D1*0x1),A2        ; A2 = base + unit*0x60
 *   00e6f4f0    clr.l D1
 *   00e6f4f2    movea.l (0x8,A6),A3           ; A3 = response
 *   00e6f4f6    move.w D0w,D1w
 *   00e6f4f8    lea (0x0,A5,D1*0x1),A1        ; A1 = SMD_GLOBALS + unit
 *   00e6f4fc    move.b (A3),(0x1d99,A1)       ; response_pending[unit] (declared at 0x1D99)
 *   00e6f500    pea (-0x20,A2)                ; &entry[unit-1].cursor_ec
 *   00e6f504    jsr 0x00e206ee.l              ; EC_$ADVANCE
 *   00e6f50a    movem.l (-0x1c,A6),{  D2 A2 A3 A5}
 *   00e6f510    unlk A6
 *   00e6f512    rts
 *
 * Bead source-fqne: A2 is base + unit*0x60 and the only field displacement is
 * negative, so the entry addressed is base + unit*0x60 - 0x60 (1-based) and
 * -0x20 from A2 is entry offset 0x40 - smd_display_hw_t::cursor_ec, the same
 * eventcount SMD_$BORROW_DISPLAY waits on (0x00E6F60E "move.l (0x40,A3),D2"
 * and 0x00E6F63A "pea (0x40,A3)").
 */

#include "smd/smd_internal.h"
#include "proc1/proc1.h"

/*
 * SMD_$SEND_RESPONSE - Send borrow response
 *
 * Parameters:
 *   response - Pointer to the response byte (a Domain boolean: negative means
 *              the borrow was granted, see SMD_$BORROW_DISPLAY 0x00E6F650)
 *
 * Does nothing if the calling process has no associated display unit.
 */
void SMD_$SEND_RESPONSE(int8_t *response)
{
    uint16_t unit;
    smd_display_info_t *info;

    /* 0x00E6F4CC-0x00E6F4D4 */
    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        return;
    }

    info = smd_$unit_info((int16_t)unit);

    /* 0x00E6F4FC: SMD_GLOBALS + 0x1D99 + unit (response_pending is
     * declared at its 0x1D99 bias slot) */
    SMD_GLOBALS.response_pending[unit] = *response;

    /* 0x00E6F500 / 0x00E6F504 */
    EC_$ADVANCE(&info->cursor_ec);
}
