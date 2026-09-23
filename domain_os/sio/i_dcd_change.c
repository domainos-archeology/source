/*
 * SIO_$I_DCD_CHANGE - DCD signal change handler
 *
 * Called from the line interrupt handler with the new carrier level as a
 * Domain boolean.  Carrier lost with DCD-hangup configured calls the
 * dcd_handler with the owner; carrier lost otherwise re-tests the byte
 * (always non-negative here, so it is a no-op) - and carrier asserted
 * restarts the transmitter.  DCD-change notification then sets the
 * pending bit and calls data_rcv, and the eventcount is always advanced.
 *
 * Original address: 0x00E1C73E, 106 bytes (SAU2 map: SIO module)
 *
 *   00e1c73e    link.w A6,0x0
 *   00e1c742    pea (A2)
 *   00e1c744    movea.l (0x8,A6),A2            ; desc
 *   00e1c748    move.b (0xc,A6),D0b            ; dcd_state
 *   00e1c74c    bmi.b 0x00e1c76c               ; asserted -> TSTART
 *   00e1c74e    btst.b #0x2,(0x53,A2)          ; params.flags2 & DCD_HANGUP
 *   00e1c754    beq.b 0x00e1c768
 *   00e1c756    tst.l (0x30,A2)                ; dcd_handler
 *   00e1c75a    beq.b 0x00e1c772
 *   00e1c75c    move.l (0x4,A2),-(SP)          ; owner
 *   00e1c760    movea.l (0x30,A2),A0
 *   00e1c764    jsr (A0)                       ; dcd_handler(owner) - no result slot
 *   00e1c766    bra.b 0x00e1c770
 *   00e1c768    tst.b D0b                      ; dcd_state again
 *   00e1c76a    bpl.b 0x00e1c772               ; (always taken on this path)
 *   00e1c76c    pea (A2)
 *   00e1c76e    bsr.b 0x00e1c7a8               ; SIO_$I_TSTART(desc)
 *   00e1c770    addq.w #0x4,SP
 *   00e1c772    btst.b #0x3,(0x57,A2)          ; params.break_mask & INT_DCD_CHANGE
 *   00e1c778    beq.b 0x00e1c796
 *   00e1c77a    bset.b #0x3,(0x67,A2)          ; pending_int |= DCD_CHANGED
 *   00e1c780    tst.l (0x38,A2)                ; data_rcv
 *   00e1c784    beq.b 0x00e1c796
 *   00e1c786    subq.l #0x2,SP                 ; word result slot
 *   00e1c788    clr.w -(SP)                    ; arg 2 = 0
 *   00e1c78a    move.l (0x4,A2),-(SP)          ; arg 1 = owner
 *   00e1c78e    movea.l (0x38,A2),A0
 *   00e1c792    jsr (A0)                       ; data_rcv(owner, 0)
 *   00e1c794    addq.w #0x8,SP
 *   00e1c796    pea (0x68,A2)
 *   00e1c79a    jsr 0x00e20718.l               ; EC_$ADVANCE_WITHOUT_DISPATCH(&desc->ec)
 *   00e1c7a0    movea.l (-0x4,A6),A2
 *   00e1c7a4    unlk A6
 *   00e1c7a6    rts
 */

#include "sio/sio_internal.h"

void SIO_$I_DCD_CHANGE(sio_desc_t *desc, int8_t dcd_state)
{
    /* 0x00E1C748-0x00E1C770 */
    if (dcd_state >= 0) {
        if ((desc->params.flags2 & SIO_CTRL_DCD_HANGUP) != 0) {
            if (desc->dcd_handler != 0) {
                ((sio_dcd_handler_fn_t)ARCH_VA_TO_PTR(desc->dcd_handler))(desc->owner);
            }
        } else if (dcd_state < 0) {
            /* 0x00E1C768-0x00E1C76A: the byte is re-tested; it is
             * non-negative on this path, so TSTART is never reached from
             * here.  Kept as the image has it. */
            SIO_$I_TSTART(desc);
        }
    } else {
        SIO_$I_TSTART(desc);
    }

    /* 0x00E1C772-0x00E1C794 */
    if ((desc->params.break_mask & SIO_INT_DCD_CHANGE) != 0) {
        desc->pending_int |= SIO_PEND_DCD_CHANGED;
        if (desc->data_rcv != 0) {
            ((sio_data_rcv_fn_t)ARCH_VA_TO_PTR(desc->data_rcv))(desc->owner, 0);
        }
    }

    /* 0x00E1C796-0x00E1C79A */
    EC_$ADVANCE_WITHOUT_DISPATCH(&desc->ec);
}
