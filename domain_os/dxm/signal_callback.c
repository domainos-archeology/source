/*
 * dxm/signal_callback.c - DXM_$ADD_SIGNAL_CALLBACK implementation
 *
 * Signal delivery callback invoked by DXM when a queued signal
 * is ready to be delivered.
 *
 * Original address: 0x00E72184
 */

#include "dxm/dxm_internal.h"

/*
 * External references for signal dispatch tables
 * These are set up by the signal/process subsystems.
 *
 * NETLOG_$DATA_END (0x00E85708) - Base address for signal handler table
 * PROC2_UID base (0x00E7BE94) - Base address for process UID data
 */
extern void (*SIGNAL_HANDLER_TABLE[])(void *, void *, void *, void *);
extern uint8_t PROC2_UID_DATA[];

#define SIGNAL_HANDLER_TABLE_BASE   0x00E85708
#define PROC2_UID_BASE              0x00E7BE94

/*
 * DXM_$ADD_SIGNAL_CALLBACK - Signal delivery callback
 *
 * Called by DXM when a queued signal is ready to be delivered.
 * Looks up the signal handler from a dispatch table and invokes it.
 *
 * Parameters:
 *   data - Pointer to pointer to signal data (dxm_signal_data_t)
 *
 * The signal data structure (10 bytes):
 *   offset 0: signal_num (2 bytes) - Index into handler table
 *   offset 2: param3 (2 bytes)
 *   offset 4: param4 (4 bytes)
 *   offset 8: param2 (2 bytes) - Index into UID data
 *
 * The function:
 * 1. Gets signal_data from *data
 * 2. Looks up handler at SIGNAL_HANDLER_TABLE[signal_num]
 * 3. Computes UID pointer at PROC2_UID_BASE + param2*8
 * 4. Calls handler(uid_ptr, &param3, &param4, &local_status)
 *
 * Assembly (0x00E72184):
 *   link.w  A6,#-0x8
 *   movem.l {A5 A3 A2},-(SP)
 *   lea     (0xe85708).l,A5      ; A5 = SIGNAL_HANDLER_TABLE_BASE
 *   movea.l (0x8,A6),A0          ; A0 = data (ptr to ptr)
 *   movea.l (A0),A2              ; A2 = *data = signal_data ptr
 *   pea     (-0x4,A6)            ; Push &local_status
 *   pea     (0x4,A2)             ; Push &signal_data->param4
 *   pea     (0x2,A2)             ; Push &signal_data->param3
 *   move.w  (0x8,A2),D0w         ; D0 = param2
 *   movea.l #0xe7be94,A1         ; A1 = PROC2_UID_BASE
 *   lsl.w   #3,D0w               ; D0 = param2 * 8
 *   pea     (0,A1,D0w)           ; Push PROC2_UID_BASE + param2*8
 *   move.w  (A2),D1w             ; D1 = signal_num
 *   lsl.w   #2,D1w               ; D1 = signal_num * 4
 *   lea     (0,A5,D1w),A1        ; A1 = &SIGNAL_HANDLER_TABLE[signal_num]
 *   movea.l (A1),A3              ; A3 = handler
 *   jsr     (A3)                 ; Call handler
 *   movem.l (-0x14,A6),{A2 A3 A5}
 *   unlk    A6
 *   rts
 */
void DXM_$ADD_SIGNAL_CALLBACK(void *data)
{
    /* Signal data is passed as pointer to pointer */
    uint16_t **data_ptr = (uint16_t **)data;
    uint16_t *signal_data = *data_ptr;

    /* Extract fields from signal data
     * signal_data layout (as uint16_t array):
     *   [0] = signal_num
     *   [1] = param3
     *   [2-3] = param4 (32-bit)
     *   [4] = param2
     */
    uint16_t signal_num = signal_data[0];
    uint16_t param2 = signal_data[4];

    /* Pointers to param3 and param4 in the signal data */
    void *param3_ptr = &signal_data[1];
    void *param4_ptr = &signal_data[2];

    /* Compute UID pointer: PROC2_UID_BASE + param2 * 8 */
    void *uid_ptr = (void *)(PROC2_UID_BASE + ((int16_t)param2 << 3));

    /* Look up signal handler: SIGNAL_HANDLER_TABLE[signal_num] */
    void (*handler)(void *, void *, void *, void *);
    handler = *(void (**)(void *, void *, void *, void *))
              (SIGNAL_HANDLER_TABLE_BASE + ((int16_t)signal_num << 2));

    /* Local status variable for handler */
    status_$t local_status;

    /* Call the signal handler */
    handler(uid_ptr, param3_ptr, param4_ptr, &local_status);
}
