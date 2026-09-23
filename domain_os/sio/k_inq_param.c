/*
 * SIO_$K_INQ_PARAM - Inquire a line's serial parameters
 *
 * Looks the line's descriptor up, copies its 0x16-byte parameter block to
 * the caller, then lets the driver's inq_params fill in the live values
 * for the selectors in *mask_ptr.  Both the lookup failure and the driver
 * status land in *status_ret.
 *
 * Original address: 0x00E6832A, 86 bytes (SAU2 map: SIO module at
 * 0xE67D9C, last routine)
 *
 *   00e6832a    link.w A6,-0x8
 *   00e6832e    movem.l {A4 A3 A2},-(SP)
 *   00e68332    movea.l (0xc,A6),A3            ; arg 2 params_ret
 *   00e68336    movea.l (0x14,A6),A4           ; arg 4 status_ret
 *   00e6833a    subq.l #0x2,SP                 ; result slot
 *   00e6833c    pea (A4)
 *   00e6833e    movea.l (0x8,A6),A0            ; arg 1 line_ptr
 *   00e68342    move.w (A0),-(SP)              ; *line_ptr
 *   00e68344    jsr 0x00e667c6.l               ; SIO_$I_GET_DESC -> A0
 *   00e6834a    addq.w #0x8,SP
 *   00e6834c    move.l A0,D0
 *   00e6834e    tst.l (A4)
 *   00e68350    bne.b 0x00e68376               ; status set -> return
 *   00e68352    movea.l D0,A2                  ; desc
 *   00e68354    lea (A3),A1
 *   00e68356    lea (0x4c,A2),A0               ; &desc->params
 *   00e6835a    moveq #0x4,D1                  ; 5 longwords
 *   00e6835c    move.l (A0)+,(A1)+
 *   00e6835e    dbf D1w,0x00e6835c
 *   00e68362    move.w (A0)+,(A1)+             ; + word
 *   00e68364    pea (A4)                       ; status_ret
 *   00e68366    movea.l (0x10,A6),A0           ; arg 3 mask_ptr
 *   00e6836a    move.l (A0),-(SP)              ; *mask_ptr by value
 *   00e6836c    pea (A3)                       ; params_ret
 *   00e6836e    move.l (A2),-(SP)              ; desc->context
 *   00e68370    movea.l (0x44,A2),A1
 *   00e68374    jsr (A1)                       ; inq_params(context, params_ret, mask, status)
 *   00e68376    movem.l (-0x14,A6),{A2 A3 A4}
 *   00e6837c    unlk A6
 *   00e6837e    rts
 */

#include "sio/sio_internal.h"

void SIO_$K_INQ_PARAM(int16_t *line_ptr, sio_params_t *params_ret,
                      const uint32_t *mask_ptr, status_$t *status_ret)
{
    sio_desc_t *desc;

    /* 0x00E6833A-0x00E68350 */
    desc = SIO_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E68354-0x00E68362: 0x16 bytes, as the image copies them */
    memcpy(params_ret, &desc->params, sizeof(sio_params_t));

    /* 0x00E68364-0x00E68374 */
    ((sio_inq_params_fn_t)ARCH_VA_TO_PTR(desc->inq_params))(
        desc->context, params_ret, *mask_ptr, status_ret);
}
