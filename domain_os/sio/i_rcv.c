/*
 * SIO_$I_RCV - Receive interrupt handler
 *
 * Called by the line driver with the received character and the hardware
 * error bits.  Errors are folded into pending_int through the enable mask
 * in params.break_mask; with the bit-5 condition pending, receive-error
 * notification configured and a special_rcv handler present, that handler
 * is called with the owner.  The character then goes to data_rcv if any
 * condition is pending and the handler exists, otherwise to rcv_handler.
 *
 * Original address: 0x00E1C620, 112 bytes (SAU2 map: SIO module, first
 * routine)
 *
 *   00e1c620    link.w A6,0x0
 *   00e1c624    pea (A2)
 *   00e1c626    movea.l (0x8,A6),A2            ; desc
 *   00e1c62a    move.l (0xe,A6),D0             ; error_flags (arg 3)
 *   00e1c62e    beq.b 0x00e1c678               ; none -> rcv_handler
 *   00e1c630    move.l D0,D1
 *   00e1c632    and.l (0x54,A2),D1             ; & params.break_mask
 *   00e1c636    or.l D1,(0x64,A2)              ; pending_int |= D1 (break_mask & error_flags)
 *   00e1c63a    btst.b #0x5,(0x67,A2)          ; pending_int bit 5
 *   00e1c640    beq.b 0x00e1c65c
 *   00e1c642    btst.b #0x3,(0x53,A2)          ; params.flags2 & RECV_ERROR
 *   00e1c648    beq.b 0x00e1c65c
 *   00e1c64a    tst.l (0x34,A2)                ; special_rcv
 *   00e1c64e    beq.b 0x00e1c65c
 *   00e1c650    move.l (0x4,A2),-(SP)          ; owner
 *   00e1c654    movea.l (0x34,A2),A0
 *   00e1c658    jsr (A0)                       ; special_rcv(owner), no result slot
 *   00e1c65a    addq.w #0x4,SP
 *   00e1c65c    tst.l (0x64,A2)                ; pending_int
 *   00e1c660    beq.b 0x00e1c678
 *   00e1c662    tst.l (0x38,A2)                ; data_rcv
 *   00e1c666    beq.b 0x00e1c678
 *   00e1c668    subq.l #0x2,SP                 ; word result slot
 *   00e1c66a    move.b (0xc,A6),-(SP)          ; char (byte, even address)
 *   00e1c66e    move.l (0x4,A2),-(SP)          ; owner
 *   00e1c672    movea.l (0x38,A2),A0           ; data_rcv
 *   00e1c676    bra.b 0x00e1c686
 *   00e1c678    subq.l #0x2,SP
 *   00e1c67a    move.b (0xc,A6),-(SP)
 *   00e1c67e    move.l (0x4,A2),-(SP)
 *   00e1c682    movea.l (0x28,A2),A0           ; rcv_handler
 *   00e1c686    jsr (A0)                       ; handler(owner, char)
 *   00e1c688    movea.l (-0x4,A6),A2
 *   00e1c68c    unlk A6
 *   00e1c68e    rts
 */

#include "sio/sio_internal.h"

void SIO_$I_RCV(sio_desc_t *desc, uint8_t char_data, uint32_t error_flags)
{
    m68k_ptr_t handler;             /* A0 at 0x00E1C686 */

    /* 0x00E1C62A-0x00E1C666 */
    if (error_flags != 0) {
        desc->pending_int |= (desc->params.break_mask & error_flags);

        if ((desc->pending_int & SIO_PEND_BIT5) != 0 &&
            (desc->params.flags2 & SIO_CTRL_RECV_ERROR) != 0 &&
            desc->special_rcv != 0) {
            ((sio_dcd_handler_fn_t)ARCH_VA_TO_PTR(desc->special_rcv))(desc->owner);
        }

        if (desc->pending_int != 0 && desc->data_rcv != 0) {
            handler = desc->data_rcv;
            goto call_handler;
        }
    }

    /* 0x00E1C678-0x00E1C682 */
    handler = desc->rcv_handler;

call_handler:
    /* 0x00E1C686 */
    ((sio_data_rcv_fn_t)ARCH_VA_TO_PTR(handler))(desc->owner, char_data);
}
