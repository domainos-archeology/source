/*
 * SIO_$I_CTS_CHANGE - CTS signal change handler
 *
 * Called from the line interrupt handler (SIO2681_$INT) with the new CTS
 * level as a Domain boolean.  CTS asserted clears the CTS-blocked bit and
 * restarts the transmitter; CTS dropped sets the bit when CTS flow control
 * is configured.  If CTS-change notification is enabled the pending word
 * gets the cts-changed bit and the data_rcv handler is called with a word
 * 0; the descriptor's eventcount is always advanced.
 *
 * Original address: 0x00E1C6DA, 100 bytes (SAU2 map: SIO module at
 * 0xE1C620, size 0x3DC)
 *
 *   00e1c6da    link.w A6,0x0
 *   00e1c6de    pea (A2)
 *   00e1c6e0    movea.l (0x8,A6),A2            ; desc
 *   00e1c6e4    move.b (0xc,A6),D0b            ; cts_state (byte in a word slot)
 *   00e1c6e8    bpl.b 0x00e1c6fa
 *   00e1c6ea    bclr.b #0x1,(0x75,A2)          ; state &= ~CTS_BLOCKED
 *   00e1c6f0    pea (A2)
 *   00e1c6f2    bsr.w 0x00e1c7a8               ; SIO_$I_TSTART(desc)
 *   00e1c6f6    addq.w #0x4,SP
 *   00e1c6f8    bra.b 0x00e1c708
 *   00e1c6fa    btst.b #0x1,(0x53,A2)          ; params.flags2 & CTS_FLOW
 *   00e1c700    beq.b 0x00e1c708
 *   00e1c702    bset.b #0x1,(0x75,A2)          ; state |= CTS_BLOCKED
 *   00e1c708    btst.b #0x4,(0x57,A2)          ; params.break_mask & INT_CTS_CHANGE
 *   00e1c70e    beq.b 0x00e1c72c
 *   00e1c710    bset.b #0x4,(0x67,A2)          ; pending_int |= CTS_CHANGED
 *   00e1c716    tst.l (0x38,A2)                ; data_rcv
 *   00e1c71a    beq.b 0x00e1c72c
 *   00e1c71c    subq.l #0x2,SP                 ; word result slot
 *   00e1c71e    clr.w -(SP)                    ; arg 2 = 0
 *   00e1c720    move.l (0x4,A2),-(SP)          ; arg 1 = owner
 *   00e1c724    movea.l (0x38,A2),A0
 *   00e1c728    jsr (A0)                       ; data_rcv(owner, 0)
 *   00e1c72a    addq.w #0x8,SP
 *   00e1c72c    pea (0x68,A2)
 *   00e1c730    jsr 0x00e20718.l               ; EC_$ADVANCE_WITHOUT_DISPATCH(&desc->ec)
 *   00e1c736    movea.l (-0x4,A6),A2
 *   00e1c73a    unlk A6
 *   00e1c73c    rts
 */

#include "sio/sio_internal.h"

void SIO_$I_CTS_CHANGE(sio_desc_t *desc, int8_t cts_state)
{
    /* 0x00E1C6E4-0x00E1C702 */
    if (cts_state < 0) {
        desc->state &= (uint16_t)~SIO_XMIT_CTS_BLOCKED;
        SIO_$I_TSTART(desc);
    } else if ((desc->params.flags2 & SIO_CTRL_CTS_FLOW) != 0) {
        desc->state |= SIO_XMIT_CTS_BLOCKED;
    }

    /* 0x00E1C708-0x00E1C72A */
    if ((desc->params.break_mask & SIO_INT_CTS_CHANGE) != 0) {
        desc->pending_int |= SIO_PEND_CTS_CHANGED;
        if (desc->data_rcv != 0) {
            ((sio_data_rcv_fn_t)ARCH_VA_TO_PTR(desc->data_rcv))(desc->owner, 0);
        }
    }

    /* 0x00E1C72C-0x00E1C730 */
    EC_$ADVANCE_WITHOUT_DISPATCH(&desc->ec);
}
