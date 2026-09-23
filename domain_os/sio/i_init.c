/*
 * SIO_$I_INIT - Reset the interrupt-level state of a descriptor
 *
 * Clears the state word, the pending-condition longword and
 * params.flags2 (the SIO_CTRL_* longword at +0x50), then initialises the
 * descriptor's eventcount.  Called from SIO_$INIT_DESC (0x00E32B16).
 *
 * Original address: 0x00E67E5E, 40 bytes (SAU2 map: SIO module at 0xE67D9C)
 *
 *   00e67e5e    link.w A6,0x0
 *   00e67e62    pea (A2)
 *   00e67e64    movea.l (0x8,A6),A2            ; desc
 *   00e67e68    clr.w (0x74,A2)                ; state = 0
 *   00e67e6c    clr.l (0x64,A2)                ; pending_int = 0
 *   00e67e70    clr.l (0x50,A2)                ; params.flags2 = 0
 *   00e67e74    pea (0x68,A2)
 *   00e67e78    jsr 0x00e151fe.l               ; EC_$INIT(&desc->ec)
 *   00e67e7e    movea.l (-0x4,A6),A2
 *   00e67e82    unlk A6
 *   00e67e84    rts
 */

#include "sio/sio_internal.h"

void SIO_$I_INIT(sio_desc_t *desc)
{
    /* 0x00E67E68-0x00E67E70 */
    desc->state = 0;
    desc->pending_int = 0;
    desc->params.flags2 = 0;

    /* 0x00E67E74-0x00E67E78 */
    EC_$INIT(&desc->ec);
}
