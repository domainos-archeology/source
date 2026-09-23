/*
 * SIO_$I_ERR - Fetch and clear one pending line condition
 *
 * Under SIO_$SPIN_LOCK, takes the pending_int bits selected by the mask
 * (0x1F when check_all is true, 0x18 - the dcd/cts bits - otherwise),
 * clears them in the descriptor, and returns the status for the first
 * one set, in the order framing, parity, overrun, dcd changed, cts
 * changed.  The bit-5 arm (0x36000B) is in the image although neither
 * mask can leave bit 5 set; Ghidra drops it as unreachable.
 *
 * Original address: 0x00E67D9C, 194 bytes (SAU2 map: "I E67D9C SIO size
 * = 5E4").  A5 = 0xE82458 = SIO_$SPIN_LOCK ("D E82458 SIO size = 4").
 *
 *   00e67d9c    link.w A6,-0xc
 *   00e67da0    movem.l {A5 A2 D2},-(SP)
 *   00e67da4    lea (0xe82458).l,A5            ; &SIO_$SPIN_LOCK
 *   00e67daa    movea.l (0x8,A6),A2            ; desc
 *   00e67dae    move.b (0xc,A6),D2b            ; check_all
 *   00e67db2    tst.l (0x64,A2)                ; pending_int
 *   00e67db6    beq.w 0x00e67e4c               ; none -> result 0
 *   00e67dba    pea (A5)
 *   00e67dbc    jsr 0x00e20bb6.l               ; ML_$SPIN_LOCK
 *   00e67dc2    addq.w #0x4,SP
 *   00e67dc4    move.w D0w,(-0xa,A6)           ; token
 *   00e67dc8    tst.b D2b
 *   00e67dca    bpl.b 0x00e67dd0
 *   00e67dcc    moveq #0x1f,D2
 *   00e67dce    bra.b 0x00e67dd2
 *   00e67dd0    moveq #0x18,D2
 *   00e67dd2    and.l (0x64,A2),D2             ; taken = pending & mask
 *   00e67dd6    move.l D2,D1
 *   00e67dd8    not.l D1
 *   00e67dda    and.l D1,(0x64,A2)             ; pending &= ~taken
 *   00e67dde    subq.l #0x2,SP                 ; result slot
 *   00e67de0    move.w D0w,-(SP)               ; token
 *   00e67de2    pea (A5)
 *   00e67de4    jsr 0x00e20bbe.l               ; ML_$SPIN_UNLOCK
 *   00e67dea    addq.w #0x8,SP
 *   00e67dec    btst.l #0x5,D2
 *   00e67df0    beq.b 0x00e67dfc
 *   00e67df2    move.l #0x36000b,(-0x8,A6)
 *   00e67dfa    bra.b 0x00e67e50
 *   00e67dfc    btst.l #0x1,D2
 *   00e67e00    beq.b 0x00e67e0c
 *   00e67e02    move.l #0x360004,(-0x8,A6)     ; framing
 *   00e67e0a    bra.b 0x00e67e50
 *   00e67e0c    btst.l #0x0,D2
 *   00e67e10    beq.b 0x00e67e1c
 *   00e67e12    move.l #0x360005,(-0x8,A6)     ; parity
 *   00e67e1a    bra.b 0x00e67e50
 *   00e67e1c    btst.l #0x2,D2
 *   00e67e20    beq.b 0x00e67e2c
 *   00e67e22    move.l #0x360009,(-0x8,A6)     ; overrun
 *   00e67e2a    bra.b 0x00e67e50
 *   00e67e2c    btst.l #0x3,D2
 *   00e67e30    beq.b 0x00e67e3c
 *   00e67e32    move.l #0x360006,(-0x8,A6)     ; dcd changed
 *   00e67e3a    bra.b 0x00e67e50
 *   00e67e3c    btst.l #0x4,D2
 *   00e67e40    beq.b 0x00e67e4c
 *   00e67e42    move.l #0x360007,(-0x8,A6)     ; cts changed
 *   00e67e4a    bra.b 0x00e67e50
 *   00e67e4c    clr.l (-0x8,A6)
 *   00e67e50    move.l (-0x8,A6),D0
 *   00e67e54    movem.l (-0x18,A6),{D2 A2 A5}
 *   00e67e5a    unlk A6
 *   00e67e5c    rts
 */

#include "sio/sio_internal.h"

status_$t SIO_$I_ERR(sio_desc_t *desc, int8_t check_all)
{
    ml_$spin_token_t token;     /* (-0xA,A6) */
    status_$t result;           /* (-0x8,A6) */
    uint32_t taken;             /* D2 */

    /* 0x00E67DB2-0x00E67DB6 */
    if (desc->pending_int == 0) {
        result = status_$ok;
        return result;
    }

    /* 0x00E67DBA-0x00E67DEA */
    token = ML_$SPIN_LOCK(&SIO_$SPIN_LOCK);
    taken = (check_all < 0) ? SIO_ERR_MASK_ALL : SIO_ERR_MASK_SIGNALS;
    taken &= desc->pending_int;
    desc->pending_int &= ~taken;
    ML_$SPIN_UNLOCK(&SIO_$SPIN_LOCK, token);

    /* 0x00E67DEC-0x00E67E50 */
    if ((taken & SIO_PEND_BIT5) != 0) {
        result = status_$sio_code_0b;
    } else if ((taken & SIO_PEND_FRAMING) != 0) {
        result = status_$sio_framing_error;
    } else if ((taken & SIO_PEND_PARITY) != 0) {
        result = status_$sio_parity_error;
    } else if ((taken & SIO_PEND_OVERRUN) != 0) {
        result = status_$sio_input_overrun;
    } else if ((taken & SIO_PEND_DCD_CHANGED) != 0) {
        result = status_$sio_dcd_changed;
    } else if ((taken & SIO_PEND_CTS_CHANGED) != 0) {
        result = status_$sio_cts_changed;
    } else {
        result = status_$ok;
    }
    return result;
}
