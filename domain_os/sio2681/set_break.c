/*
 * SIO2681_$SET_BREAK - Start or stop a break on a channel
 *
 * Under the driver's spin lock, writes the start-break command (true) or
 * the stop-break command followed by a SIO_$I_TSTART of the channel's
 * descriptor (false).
 *
 * Original address: 0x00E1D114, 94 bytes (SAU2 map: SIO_IO segment).
 * A5 = 0xE2DEB8 = SIO2681_$DATA, whose first longword is the lock.
 *
 *   00e1d114    link.w A6,-0x4
 *   00e1d118    movem.l {A5 A2 D2},-(SP)
 *   00e1d11c    lea (0xe2deb8).l,A5
 *   00e1d122    movea.l (0x8,A6),A2            ; channel
 *   00e1d126    move.b (0xc,A6),D2b            ; enable (byte in a word slot)
 *   00e1d12a    pea (A5) ; jsr ML_$SPIN_LOCK ; addq.w #4,SP ; move.w D0w,(-0x2,A6)
 *   00e1d138    tst.b D2b ; bpl 0x00e1d146
 *   00e1d13c    movea.l (A2),A0 ; move.b (0x4a,A5),(0x5,A0)   ; CR = start break (0x60)
 *   00e1d144    bra.b 0x00e1d15a
 *   00e1d146    movea.l (A2),A0 ; move.b (0x48,A5),(0x5,A0)   ; CR = stop break (0x70)
 *   00e1d14e    move.l (0xc,A2),-(SP) ; jsr SIO_$I_TSTART ; addq.w #4,SP
 *   00e1d15a    subq.l #2,SP ; move.w (-0x2,A6),-(SP) ; pea (A5) ; jsr ML_$SPIN_UNLOCK
 *   00e1d168    movem.l (-0x10,A6),{D2 A2 A5} ; unlk ; rts
 */

#include "sio2681/sio2681_internal.h"

void SIO2681_$SET_BREAK(sio2681_channel_t *channel, int8_t enable)
{
    ml_$spin_token_t token;         /* (-0x2,A6) */

    /* 0x00E1D12A-0x00E1D134 */
    token = ML_$SPIN_LOCK(&SIO2681_$DATA.spin_lock);

    /* 0x00E1D138-0x00E1D158 */
    if (enable < 0) {
        channel->regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_break_start;
    } else {
        channel->regs[SIO2681_REG_CRA] = SIO2681_$DATA.cmd_break_stop;
        SIO_$I_TSTART(channel->sio_desc);
    }

    /* 0x00E1D15A-0x00E1D162 */
    ML_$SPIN_UNLOCK(&SIO2681_$DATA.spin_lock, token);
}
