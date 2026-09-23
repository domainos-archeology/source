/*
 * SIO_$I_INHIBIT_RCV - Receive inhibit (XOFF/XON towards the remote)
 *
 * With software flow control configured (params.flags2 bit 0), clears
 * params.flags1 bit 0 to inhibit or sets it to release and hands the
 * parameter block to the driver's set_params with change mask 0x20.
 * When update_xmit is true, the transmit state's deferred-inhibit bits are
 * adjusted and the transmitter is restarted.
 *
 * Original address: 0x00E1C94A, 132 bytes (SAU2 map: SIO module)
 *
 *   00e1c94a    link.w A6,-0x4
 *   00e1c94e    movem.l {A2 D3 D2},-(SP)
 *   00e1c952    movea.l (0x8,A6),A2            ; desc
 *   00e1c956    move.b (0xc,A6),D2b            ; inhibit
 *   00e1c95a    move.b (0xe,A6),D3b            ; update_xmit
 *   00e1c95e    btst.b #0x0,(0x53,A2)          ; params.flags2 & SOFT_FLOW
 *   00e1c964    beq.b 0x00e1c990
 *   00e1c966    tst.b D2b
 *   00e1c968    bpl.b 0x00e1c972
 *   00e1c96a    bclr.b #0x0,(0x4f,A2)          ; flags1 &= ~1
 *   00e1c970    bra.b 0x00e1c978
 *   00e1c972    bset.b #0x0,(0x4f,A2)          ; flags1 |= 1
 *   00e1c978    pea (-0x4,A6)                  ; &status
 *   00e1c97c    pea (0x20).w                   ; mask 0x20 by value
 *   00e1c980    pea (0x4c,A2)                  ; &params
 *   00e1c984    move.l (A2),-(SP)              ; context
 *   00e1c986    movea.l (0x40,A2),A0
 *   00e1c98a    jsr (A0)                       ; set_params(context, &params, 0x20, &status)
 *   00e1c98c    lea (0x10,SP),SP
 *   00e1c990    tst.b D3b
 *   00e1c992    bpl.b 0x00e1c9c4
 *   00e1c994    tst.b D2b
 *   00e1c996    bpl.b 0x00e1c9ac
 *   00e1c998    bclr.b #0x5,(0x75,A2)          ; state &= ~DEFER_INHIBIT
 *   00e1c99e    tst.b (0x75,A2)
 *   00e1c9a2    bmi.b 0x00e1c9be               ; DEFER_COMPLETE set -> skip
 *   00e1c9a4    bset.b #0x6,(0x75,A2)          ; state |= DEFER_PENDING
 *   00e1c9aa    bra.b 0x00e1c9be
 *   00e1c9ac    tst.b (0x75,A2)
 *   00e1c9b0    bpl.b 0x00e1c9b8
 *   00e1c9b2    bset.b #0x5,(0x75,A2)          ; state |= DEFER_INHIBIT
 *   00e1c9b8    bclr.b #0x6,(0x75,A2)          ; state &= ~DEFER_PENDING
 *   00e1c9be    pea (A2)
 *   00e1c9c0    bsr.w 0x00e1c7a8               ; SIO_$I_TSTART(desc)
 *   00e1c9c4    movem.l (-0x10,A6),{D2 D3 A2}
 *   00e1c9ca    unlk A6
 *   00e1c9cc    rts
 */

#include "sio/sio_internal.h"

void SIO_$I_INHIBIT_RCV(sio_desc_t *desc, int8_t inhibit, int8_t update_xmit)
{
    status_$t status;               /* (-0x4,A6) */

    /* 0x00E1C95E-0x00E1C98C */
    if ((desc->params.flags2 & SIO_CTRL_SOFT_FLOW) != 0) {
        if (inhibit < 0) {
            desc->params.flags1 &= ~(uint32_t)SIO_FLAGS1_RCV_ENABLED;
        } else {
            desc->params.flags1 |= SIO_FLAGS1_RCV_ENABLED;
        }
        ((sio_set_params_fn_t)ARCH_VA_TO_PTR(desc->set_params))(
            desc->context, &desc->params, SIO_PARAM_SOFT_FLOW, &status);
    }

    /* 0x00E1C990-0x00E1C9C0 */
    if (update_xmit < 0) {
        if (inhibit < 0) {
            desc->state &= (uint16_t)~SIO_XMIT_DEFER_INHIBIT;
            if ((desc->state & SIO_XMIT_DEFER_COMPLETE) == 0) {
                desc->state |= SIO_XMIT_DEFER_PENDING;
            }
        } else {
            if ((desc->state & SIO_XMIT_DEFER_COMPLETE) != 0) {
                desc->state |= SIO_XMIT_DEFER_INHIBIT;
            }
            desc->state &= (uint16_t)~SIO_XMIT_DEFER_PENDING;
        }
        SIO_$I_TSTART(desc);
    }
}
