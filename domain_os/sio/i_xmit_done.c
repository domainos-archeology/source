/*
 * SIO_$I_XMIT_DONE - Transmit-complete interrupt handler
 *
 * Clears the transmit-active bit, runs the transmit state machine, and
 * returns (as a Domain boolean) whether a transmission is active again.
 * Called from SIO2681_$INT (0x00E1CFEC, 0x00E1D048).
 *
 * Original address: 0x00E1C6B4, 38 bytes (SAU2 map: SIO module)
 *
 *   00e1c6b4    link.w A6,-0x4
 *   00e1c6b8    pea (A2)
 *   00e1c6ba    movea.l (0x8,A6),A2            ; desc
 *   00e1c6be    bclr.b #0x0,(0x75,A2)          ; state &= ~XMIT_ACTIVE
 *   00e1c6c4    pea (A2)
 *   00e1c6c6    bsr.w 0x00e1c7a8               ; SIO_$I_TSTART(desc)
 *   00e1c6ca    btst.b #0x0,(0x75,A2)          ; state & XMIT_ACTIVE
 *   00e1c6d0    sne D0b                        ; result = 0xFF if set
 *   00e1c6d2    movea.l (-0x8,A6),A2
 *   00e1c6d6    unlk A6
 *   00e1c6d8    rts
 */

#include "sio/sio_internal.h"

boolean SIO_$I_XMIT_DONE(sio_desc_t *desc)
{
    /* 0x00E1C6BE-0x00E1C6C6 */
    desc->state &= (uint16_t)~SIO_XMIT_ACTIVE;
    SIO_$I_TSTART(desc);

    /* 0x00E1C6CA-0x00E1C6D0 */
    return ((desc->state & SIO_XMIT_ACTIVE) != 0) ? true : false;
}
