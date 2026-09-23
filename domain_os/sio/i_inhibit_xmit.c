/*
 * SIO_$I_INHIBIT_XMIT - Transmit inhibit (XOFF/XON from the remote)
 *
 * Sets the inhibited bit of the transmit state, or clears it and restarts
 * the transmitter.  A procedure; its only reference is the pointer cell at
 * 0x00E2CA3C in TERM_$DATA.
 *
 * Original address: 0x00E1C9CE, 44 bytes (SAU2 map: SIO module)
 *
 *   00e1c9ce    link.w A6,0x0
 *   00e1c9d2    pea (A2)
 *   00e1c9d4    movea.l (0x8,A6),A2            ; desc
 *   00e1c9d8    move.b (0xc,A6),D0b            ; inhibit
 *   00e1c9dc    bpl.b 0x00e1c9e6
 *   00e1c9de    bset.b #0x2,(0x75,A2)          ; state |= INHIBITED
 *   00e1c9e4    bra.b 0x00e1c9f2
 *   00e1c9e6    bclr.b #0x2,(0x75,A2)          ; state &= ~INHIBITED
 *   00e1c9ec    pea (A2)
 *   00e1c9ee    bsr.w 0x00e1c7a8               ; SIO_$I_TSTART(desc) (args left for unlk)
 *   00e1c9f2    movea.l (-0x4,A6),A2
 *   00e1c9f6    unlk A6
 *   00e1c9f8    rts
 */

#include "sio/sio_internal.h"

void SIO_$I_INHIBIT_XMIT(sio_desc_t *desc, int8_t inhibit)
{
    /* 0x00E1C9D8-0x00E1C9EE */
    if (inhibit < 0) {
        desc->state |= SIO_XMIT_INHIBITED;
    } else {
        desc->state &= (uint16_t)~SIO_XMIT_INHIBITED;
        SIO_$I_TSTART(desc);
    }
}
