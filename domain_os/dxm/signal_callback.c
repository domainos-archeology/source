/*
 * dxm/signal_callback.c - DXM_$ADD_SIGNAL_CALLBACK implementation
 *
 * Signal delivery callback invoked by the DXM unwired helper process when
 * a queued signal record is ready to be delivered.
 *
 * Original address: 0x00E72184 (70 bytes)
 *
 * Full instruction trace:
 *   00e72184  link.w A6,-0x8
 *   00e72188  movem.l {A5 A3 A2},-(SP)
 *   00e7218c  lea (0xe85708).l,A5      ; A5 = DXM_$SIGNAL_ROUTINES
 *   00e72192  movea.l (0x8,A6),A0      ; A0 = data (pointer to pointer)
 *   00e72196  movea.l (A0),A2          ; A2 = *data = &signal record
 *   00e72198  pea (-0x4,A6)            ; &local_status
 *   00e7219c  pea (0x4,A2)             ; &rec->param
 *   00e721a0  pea (0x2,A2)             ; &rec->signal
 *   00e721a4  move.w (0x8,A2),D0w      ; rec->proc_index
 *   00e721a8  movea.l #0xe7be94,A1     ; A1 = PROC2_UID
 *   00e721ae  lsl.w #0x3,D0w           ; *8 == sizeof(uid_t)
 *   00e721b0  pea (0x0,A1,D0w*0x1)     ; &PROC2_UID[rec->proc_index]
 *   00e721b4  move.w (A2),D1w          ; rec->routine
 *   00e721b6  lsl.w #0x2,D1w           ; *4 == sizeof(routine pointer)
 *   00e721b8  lea (0x0,A5,D1w*0x1),A1
 *   00e721bc  movea.l (A1),A3          ; A3 = DXM_$SIGNAL_ROUTINES[routine]
 *   00e721be  jsr (A3)
 *   00e721c0  movem.l (-0x14,A6),{A2 A3 A5}
 *   00e721c6  unlk A6
 *   00e721c8  rts
 *
 * The 16 bytes of arguments are left on the stack by the callee (Pascal
 * convention), which the `unlk A6` then discards.  The status word written
 * by the routine goes to a dead stack local at (-0x4,A6): the deferred
 * caller has already returned, so there is nowhere to report it.
 *
 * The indices are NOT range checked; a bad record index reads outside both
 * tables.  That is the behaviour of the original.
 */

#include "dxm/dxm_internal.h"

void DXM_$ADD_SIGNAL_CALLBACK(void *data)
{
    dxm_signal_data_t *rec;
    dxm_$signal_routine_t routine;
    status_$t local_status;

    /* 0x00E72192/0x00E72196: the queue entry holds a pointer to the record */
    rec = *(dxm_signal_data_t **)data;

    /* 0x00E721B4-0x00E721BC */
    routine = DXM_$SIGNAL_ROUTINES[rec->routine];

    /* 0x00E721BE */
    routine(&PROC2_UID[rec->proc_index], &rec->signal, &rec->param,
            &local_status);
}
