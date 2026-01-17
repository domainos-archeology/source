/*
 * dxm/add_signal.c - DXM_$ADD_SIGNAL implementation
 *
 * Queues a signal for deferred delivery.
 *
 * Original address: 0x00E17270
 */

#include "dxm/dxm_internal.h"

/*
 * Signal data structure used by ADD_SIGNAL
 * This structure is passed to DXM_$ADD_SIGNAL_CALLBACK
 * Size: 10 bytes
 */
typedef struct dxm_signal_data_t {
    uint16_t    signal_num;     /* 0x00: Signal number */
    uint16_t    param3;         /* 0x02: Signal parameter 3 */
    uint32_t    param4;         /* 0x04: Signal parameter 4 */
    uint16_t    param2;         /* 0x08: Signal parameter 2 */
} dxm_signal_data_t;

/*
 * DXM_$ADD_SIGNAL - Queue a signal for deferred delivery
 *
 * Adds a signal delivery callback to the unwired queue.
 * The signal will be delivered later by the unwired helper process.
 *
 * Parameters:
 *   signal_num - Signal number
 *   param2 - Signal parameter 2
 *   param3 - Signal parameter 3
 *   param4 - Signal parameter 4 (4 bytes)
 *   param5 - Additional flag (passed as high byte of flags)
 *   status_ret - Status return
 *
 * The function packages the signal parameters into a dxm_signal_data_t
 * structure and queues it with DXM_$ADD_SIGNAL_CALLBACK as the callback.
 *
 * Assembly (0x00E17270):
 *   link.w  A6,#-0x14
 *   movem.l {A5 D3 D2},-(SP)
 *   lea     (0xe2a7c0).l,A5      ; A5 = base data address
 *   move.w  (0x8,A6),D0w         ; D0 = signal_num
 *   move.w  (0xa,A6),D1w         ; D1 = param2
 *   move.w  (0xc,A6),D2w         ; D2 = param3
 *   move.l  (0xe,A6),D3          ; D3 = param4
 *   move.w  D0w,(-0x10,A6)       ; local.signal_num = D0
 *   move.w  D1w,(-0x8,A6)        ; local.param2 = D1
 *   move.w  D2w,(-0xe,A6)        ; local.param3 = D2
 *   move.l  D3,(-0xc,A6)         ; local.param4 = D3
 *   move.l  (0x14,A6),-(SP)      ; Push status_ret
 *   move.b  (0x12,A6),-(SP)      ; Push param5 (as byte)
 *   move.w  #0xa,-(SP)           ; Push data_size = 10
 *   lea     (-0x10,A6),A0        ; A0 = &local
 *   move.l  A0,(-0x14,A6)        ; local_ptr = &local
 *   pea     (-0x14,A6)           ; Push &local_ptr
 *   pea     PTR_DXM_$ADD_SIGNAL_CALLBACK ; Push callback ptr address
 *   pea     (0x604,A5)           ; Push &DXM_$UNWIRED_Q
 *   bsr.w   DXM_$ADD_CALLBACK
 *   movem.l (-0x20,A6),{D2 D3 A5}
 *   unlk    A6
 *   rts
 */
void DXM_$ADD_SIGNAL(uint16_t signal_num, uint16_t param2, uint16_t param3,
                     uint32_t param4, uint8_t param5, status_$t *status_ret)
{
    dxm_signal_data_t signal_data;
    dxm_signal_data_t *data_ptr;
    uint32_t flags;

    /* Package signal parameters */
    signal_data.signal_num = signal_num;
    signal_data.param2 = param2;
    signal_data.param3 = param3;
    signal_data.param4 = param4;

    /* Set up pointer for ADD_CALLBACK */
    data_ptr = &signal_data;

    /* Flags: data size = 10 (0x0A), plus param5 in high byte */
    flags = 10 | ((uint32_t)param5 << 16);

    /* Queue the signal callback */
    DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q,
                      (void **)&PTR_DXM_$ADD_SIGNAL_CALLBACK,
                      (void **)&data_ptr,
                      flags,
                      status_ret);
}
