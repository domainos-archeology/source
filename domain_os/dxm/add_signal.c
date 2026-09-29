/*
 * dxm/add_signal.c - DXM_$ADD_SIGNAL implementation
 *
 * Queues a signal for deferred delivery.
 *
 * Original address: 0x00E17270
 */

#include "dxm/dxm_internal.h"

/*
 * The 10-byte signal record (dxm_signal_data_t) is declared in dxm/dxm.h
 * because DXM_$ADD_SIGNAL_CALLBACK reads it back out of the queue.
 */

/*
 * DXM_$ADD_SIGNAL - Queue a signal for deferred delivery
 *
 * Adds a signal delivery callback to the unwired queue.
 * The signal will be delivered later by the unwired helper process.
 *
 * Parameters:
 *   routine    - index into DXM_$SIGNAL_ROUTINES (0 = PROC2_$SIGNAL_OS,
 *                1 = PROC2_$SIGNAL_PGROUP_OS)
 *   proc_index - index into PROC2_$UNWIRED_DATA.uid identifying the target
 *   signal     - signal number
 *   param      - signal parameter (4 bytes)
 *   check_dup  - Domain boolean (0xFF) -> DXM_$ADD_CALLBACK dedupes
 *   status_ret - Status return
 *
 * The function packages the signal parameters into a dxm_signal_data_t
 * structure and queues it with DXM_$ADD_SIGNAL_CALLBACK as the callback.
 *
 * Assembly (0x00E17270):
 *   link.w  A6,#-0x14
 *   movem.l {A5 D3 D2},-(SP)
 *   lea     (0xe2a7c0).l,A5      ; A5 = base data address
 *   move.w  (0x8,A6),D0w         ; D0 = routine
 *   move.w  (0xa,A6),D1w         ; D1 = proc_index
 *   move.w  (0xc,A6),D2w         ; D2 = signal
 *   move.l  (0xe,A6),D3          ; D3 = param
 *   move.w  D0w,(-0x10,A6)       ; rec.routine = D0
 *   move.w  D1w,(-0x8,A6)        ; rec.proc_index = D1
 *   move.w  D2w,(-0xe,A6)        ; rec.signal = D2
 *   move.l  D3,(-0xc,A6)         ; rec.param = D3
 *   move.l  (0x14,A6),-(SP)      ; Push status_ret
 *   move.b  (0x12,A6),-(SP)      ; Push check_dup (as byte)
 *   move.w  #0xa,-(SP)           ; Push data_size = 10
 *   lea     (-0x10,A6),A0        ; A0 = &local
 *   move.l  A0,(-0x14,A6)        ; local_ptr = &local
 *   pea     (-0x14,A6)           ; Push &local_ptr
 *   pea     (0x14,PC)            ; 0x00E172CC, the callback-address cell
 *   pea     (0x604,A5)           ; Push &DXM_$UNWIRED_Q
 *   bsr.w   DXM_$ADD_CALLBACK
 *   movem.l (-0x20,A6),{D2 D3 A5}
 *   unlk    A6
 *   rts
 */
void DXM_$ADD_SIGNAL(uint16_t routine, uint16_t proc_index, uint16_t signal,
                     uint32_t param, boolean check_dup, status_$t *status_ret)
{
    dxm_signal_data_t signal_data;
    dxm_signal_data_t *data_ptr;

    /*
     * Package the signal parameters.  Note the argument order on the stack
     * is (routine, proc_index, signal, param, check_dup, status_ret) but
     * the record order is routine, signal, param, proc_index:
     *   (0x8,A6)  -> rec+0x00 (routine)
     *   (0xa,A6)  -> rec+0x08 (proc_index)
     *   (0xc,A6)  -> rec+0x02 (signal)
     *   (0xe,A6)  -> rec+0x04 (param)
     */
    signal_data.routine = (int16_t)routine;
    signal_data.proc_index = (int16_t)proc_index;
    signal_data.signal = (int16_t)signal;
    signal_data.param = param;

    /* Set up pointer for ADD_CALLBACK */
    data_ptr = &signal_data;

    /*
     * Queue the signal callback.  data_size is the literal 10
     * (`move.w #0xa,-(SP)` at 0x00E172A6) and check_dup is this function's
     * own boolean parameter (`move.b (0x12,A6),-(SP)` at 0x00E172A2); they
     * are two distinct Pascal parameters, not a packed longword.  The
     * callback argument is the ADDRESS of the in-code cell at 0x00E172CC
     * ("pea (0x14,PC)" at 0x00E172B6), which holds 0x00E72184.
     */
    DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q,
                      &DXM_$ADD_SIGNAL_CALLBACK_CELL,
                      (void **)&data_ptr,
                      10,
                      check_dup,
                      status_ret);
}
