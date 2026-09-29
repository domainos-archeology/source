/*
 * SIO_$K_SIGNAL_WAIT - Wait for a modem signal
 *
 * Original address: 0x00E67FBE, 238 bytes (SAU2 map: SIO module at
 * 0xE67D9C).  The word at 0x00E67FBC (00 00) that precedes the entry is
 * SIO_$K_TIMED_BREAK's delay-type cell, not part of this routine.
 *
 * Frame: -0x10/-0xC the two eventcount pointers, -0x8/-0x4 the two wait
 * values (the arrays EC_$WAITN is handed).
 *
 *   00e67fbe    link.w A6,-0x20
 *   00e67fc2    movem.l {A4 A3 A2 D7 D6 D5 D4 D3 D2},-(SP)
 *   00e67fc6    move.l (0xc,A6),D2             ; arg 2 signals_ptr
 *   00e67fca    movea.l (0x10,A6),A3           ; arg 3 status_ret
 *   00e67fce    subq.l #0x2,SP ; pea (A3) ; move.w (*line_ptr),-(SP)
 *   00e67fd8    jsr 0x00e667c6.l               ; SIO_$I_GET_DESC -> A0
 *   00e67fe2    tst.l (A3) ; bne exit
 *   00e67fe8    movea.l D0,A2                  ; desc
 *   00e67fea    D3 = desc+0x68 (&ec)  A4 = &PROC1_$AS_ID  D5 = desc+0x44 (&inq_params)
 *   00e67ff8    D6 = desc+0x4C (&params)
 *   --- loop ---
 *   00e68000    move.l D3,(-0x10,A6)           ; ecs[0] = &desc->ec
 *   00e68008    move.l (A1),D1 ; addq.l #1,D1 ; move.l D1,(-0x8,A6)   ; vals[0] = ec.value+1
 *   00e68010    asid*12 -> (-0xc,A6) = &FIM_$WIRED_DATA.quit_ec[asid]          ; ecs[1]
 *   00e68028    asid*4 -> (-0x4,A6) = FIM_$WIRED_DATA.quit_value[asid]+1      ; vals[1]
 *   00e6803c    pea (A3) ; pea (0x180).w ; move.l D6,-(SP) ; move.l (A2),-(SP)
 *   00e6804a    jsr (A1)                       ; inq_params(context, &desc->params, 0x180, status)
 *   00e68050    tst.l (A3) ; bne exit
 *   00e68054    D0 = desc->params.flags1 & *signals_ptr ; bne exit
 *   00e6805e    subq.l #0x2,SP ; move.w #2,-(SP) ; pea (-0x8,A6) ; pea (-0x10,A6)
 *   00e6806c    jsr 0x00e2063e.l               ; EC_$WAITN(ecs, vals, 2)
 *   00e68076    cmpi.w #0x2,D0w ; bne loop
 *   00e6807c    move.l #0x36000a,(A3)          ; quit while waiting
 *   00e68082    FIM_$WIRED_DATA.quit_value[asid] = FIM_$WIRED_DATA.quit_ec[asid].value
 *   00e680a2    movem.l (-0x44,A6),{D2 D3 D4 D5 D6 D7 A2 A3 A4} ; unlk ; rts
 */

#include "sio/sio_internal.h"

void SIO_$K_SIGNAL_WAIT(int16_t *line_ptr, uint32_t *signals_ptr,
                        status_$t *status_ret)
{
    sio_desc_t *desc;
    ec_$eventcount_t *ecs[2];       /* (-0x10,A6) */
    int32_t vals[2];                /* (-0x8,A6) */
    int16_t as_id;

    /* 0x00E67FCE-0x00E67FE4 */
    desc = SIO_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    for (;;) {
        /* 0x00E68000-0x00E68038: PROC1_$AS_ID is re-read each pass */
        as_id = (int16_t)PROC1_$AS_ID;
        ecs[0] = &desc->ec;
        vals[0] = desc->ec.value + 1;
        ecs[1] = (ec_$eventcount_t *)&FIM_$WIRED_DATA.quit_ec[as_id];
        vals[1] = FIM_$WIRED_DATA.quit_value[as_id] + 1;

        /* 0x00E6803C-0x00E68052: the driver refreshes desc->params in place */
        ((sio_inq_params_fn_t)ARCH_VA_TO_PTR(desc->inq_params))(
            desc->context, &desc->params, 0x180, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }

        /* 0x00E68054-0x00E6805C */
        if ((desc->params.flags1 & *signals_ptr) != 0) {
            return;
        }

        /* 0x00E6805E-0x00E6807A */
        if (EC_$WAITN(ecs, vals, 2) == 2) {
            break;
        }
    }

    /* 0x00E6807C-0x00E6809C */
    *status_ret = status_$sio_quit_while_waiting;
    as_id = (int16_t)PROC1_$AS_ID;
    FIM_$WIRED_DATA.quit_value[as_id] = FIM_$WIRED_DATA.quit_ec[as_id].value;
}
