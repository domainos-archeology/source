/*
 * sio_$set_break - Set or clear the break state of a line
 *
 * Under SIO_$SPIN_LOCK, sets or clears the break-active bit of the
 * descriptor's state word (raising break also clears transmit-active), then
 * calls the driver's set-break entry with the same flag.
 *
 * Original address: 0x00E67E86, 90 bytes (SAU2 map: SIO module at 0xE67D9C;
 * module-local, no map symbol).
 *
 * A5 dependency: the routine locks and unlocks through `pea (A5)`
 * (0x00E67E96 / 0x00E67EC0) without loading A5 itself.  Its only callers
 * are the two bsr's in SIO_$K_TIMED_BREAK (0x00E67F14 / 0x00E67FAE), which
 * sets A5 = 0xE82458 = SIO_$SPIN_LOCK at entry (0x00E67EE8), so the cell
 * locked is SIO_$SPIN_LOCK, the same one SIO_$I_ERR locks.  The C makes
 * that dependency explicit; any new caller must hold the same expectation.
 *
 *   00e67e86    link.w A6,-0x8
 *   00e67e8a    movem.l {A2 D2},-(SP)
 *   00e67e8e    movea.l (0x8,A6),A2            ; desc
 *   00e67e92    move.b (0xc,A6),D2b            ; enable (byte in a word slot)
 *   00e67e96    pea (A5)                       ; &SIO_$SPIN_LOCK (caller's A5)
 *   00e67e98    jsr 0x00e20bb6.l               ; ML_$SPIN_LOCK
 *   00e67e9e    addq.w #0x4,SP
 *   00e67ea0    move.w D0w,(-0x6,A6)           ; token (D0 is what gets pushed back)
 *   00e67ea4    tst.b D2b
 *   00e67ea6    bpl.b 0x00e67eb6
 *   00e67ea8    bclr.b #0x0,(0x75,A2)          ; state &= ~SIO_XMIT_ACTIVE
 *   00e67eae    bset.b #0x3,(0x75,A2)          ; state |= SIO_STATE_BREAK_ACTIVE
 *   00e67eb4    bra.b 0x00e67ebc
 *   00e67eb6    bclr.b #0x3,(0x75,A2)          ; state &= ~SIO_STATE_BREAK_ACTIVE
 *   00e67ebc    subq.l #0x2,SP
 *   00e67ebe    move.w D0w,-(SP)               ; token
 *   00e67ec0    pea (A5)
 *   00e67ec2    jsr 0x00e20bbe.l               ; ML_$SPIN_UNLOCK
 *   00e67ec8    addq.w #0x8,SP
 *   00e67eca    subq.l #0x2,SP                 ; word result slot, never read
 *   00e67ecc    move.b D2b,-(SP)               ; enable
 *   00e67ece    move.l (A2),-(SP)              ; desc->context
 *   00e67ed0    movea.l (0x48,A2),A0
 *   00e67ed4    jsr (A0)                       ; desc->set_break (args left for unlk)
 *   00e67ed6    movem.l (-0x10,A6),{D2 A2}
 *   00e67edc    unlk A6
 *   00e67ede    rts
 */

#include "sio/sio_internal.h"

void sio_$set_break(sio_desc_t *desc, int8_t enable)
{
    ml_$spin_token_t token;         /* (-0x6,A6) */

    /* 0x00E67E96-0x00E67EA0 */
    token = ML_$SPIN_LOCK(&SIO_$SPIN_LOCK);

    /* 0x00E67EA4-0x00E67EB6: byte ops on (0x75,A2), the low byte of state */
    if (enable < 0) {
        desc->state &= (uint16_t)~SIO_XMIT_ACTIVE;
        desc->state |= SIO_STATE_BREAK_ACTIVE;
    } else {
        desc->state &= (uint16_t)~SIO_STATE_BREAK_ACTIVE;
    }

    /* 0x00E67EBC-0x00E67EC8 */
    ML_$SPIN_UNLOCK(&SIO_$SPIN_LOCK, token);

    /* 0x00E67ECA-0x00E67ED4 */
    ((sio_set_break_fn_t)ARCH_VA_TO_PTR(desc->set_break))(desc->context, enable);
}
