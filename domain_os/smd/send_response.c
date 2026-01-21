/*
 * smd/send_response.c - SMD_$SEND_RESPONSE implementation
 *
 * Sends a response back to a process waiting for display borrow completion.
 *
 * Original address: 0x00E6F4BE
 *
 * Assembly:
 *   00e6f4be    link.w A6,-0xc
 *   00e6f4c2    movem.l {  A5 A3 A2 D2},-(SP)
 *   00e6f4c6    lea (0xe82b8c).l,A5          ; Load SMD globals base
 *   00e6f4cc    move.w (0x00e2060a).l,D0w    ; Get current process ASID
 *   00e6f4d2    add.w D0w,D0w                ; ASID * 2
 *   00e6f4d4    move.w (0x48,A5,D0w*0x1),D0w ; Get unit from asid_to_unit table
 *   00e6f4d8    beq.b 0x00e6f50a             ; If 0, no unit - skip
 *   00e6f4da    clr.l D1
 *   00e6f4dc    movea.l #0xe27376,A0         ; Display info base
 *   00e6f4e2    move.w D0w,D1w
 *   00e6f4e4    lsl.l #0x5,D1                ; unit * 32
 *   00e6f4e6    move.l D1,D2
 *   00e6f4e8    add.l D2,D2                  ; * 2 = unit * 64
 *   00e6f4ea    add.l D2,D1                  ; + unit * 32 = unit * 96 (0x60)
 *   00e6f4ec    lea (0x0,A0,D1*0x1),A2       ; Display info[unit]
 *   00e6f4f0    clr.l D1
 *   00e6f4f2    movea.l (0x8,A6),A3          ; Get response pointer
 *   00e6f4f6    move.w D0w,D1w
 *   00e6f4f8    lea (0x0,A5,D1*0x1),A1       ; SMD_GLOBALS + unit
 *   00e6f4fc    move.b (A3),(0x1d99,A1)      ; Store response at offset 0x1D99
 *   00e6f500    pea (-0x20,A2)               ; Display info[unit] - 0x20
 *   00e6f504    jsr 0x00e206ee.l             ; Call EC_$ADVANCE
 *   00e6f50a    movem.l (-0x1c,A6),{  D2 A2 A3 A5}
 *   00e6f510    unlk A6
 *   00e6f512    rts
 *
 * The response byte is stored at SMD_GLOBALS + unit + 0x1D99.
 * Then the event count at (display_info[unit] - 0x20) is advanced to wake
 * the waiting process.
 */

#include "smd/smd_internal.h"
#include "proc1/proc1.h"

/*
 * SMD_$SEND_RESPONSE - Send borrow response
 *
 * Sends a response to a process that is waiting for a display borrow
 * operation to complete. The response byte indicates the result.
 *
 * This function:
 * 1. Looks up the display unit for the current process
 * 2. Stores the response byte at a per-unit location
 * 3. Signals the per-unit event count to wake the waiter
 *
 * Parameters:
 *   response - Pointer to response byte
 *
 * Note: This function does nothing if the current process has no
 * associated display unit.
 */
void SMD_$SEND_RESPONSE(int8_t *response)
{
    uint16_t unit;
    uint32_t unit_offset;
    ec_$eventcount_t *ec;

    /* Get display unit for current process */
    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    /* If no unit associated, nothing to do */
    if (unit == 0) {
        return;
    }

    /* Calculate display info offset: unit * 0x60 */
    unit_offset = (uint32_t)unit * SMD_DISPLAY_INFO_SIZE;

    /* Store response byte at SMD_GLOBALS + unit + 0x1D99 */
    SMD_BORROW_RESPONSE[unit] = *response;

    /* Calculate event count address: display_info[unit] - 0x20 */
    ec = (ec_$eventcount_t *)((uint8_t *)&SMD_DISPLAY_INFO[0] + unit_offset - 0x20);

    /* Advance event count to signal waiter */
    EC_$ADVANCE(ec);
}
