/*
 * dxm/dxm_data.c - Deferred Execution Manager Global Data
 *
 * Defines global variables used by the DXM subsystem.
 *
 * The DXM data is located in a block starting at 0x00E2A7C0.
 * Key offsets from that base:
 *   0x600 (0xE2ADC0): DXM_$OVERRUNS
 *   0x604 (0xE2ADC4): DXM_$UNWIRED_Q
 *   0x620 (0xE2ADE0): DXM_$WIRED_Q
 *   0x62C (0xE2ADEC): Wired queue event count (initialized by DXM_$INIT)
 *   0x610 (0xE2ADD0): Unwired queue event count (initialized by DXM_$INIT)
 */

#include "dxm/dxm_internal.h"

/*
 * Queue overflow counter
 * Incremented each time a callback cannot be added due to queue full.
 * Original address: 0x00E2ADC0
 */
uint32_t DXM_$OVERRUNS = 0;

/*
 * Unwired callback queue
 * For callbacks that run with resource lock 0x03 held.
 * Original address: 0x00E2ADC4
 *
 * Note: The queue entry arrays and event counts are set up
 * during system initialization, not here.
 */
dxm_queue_t DXM_$UNWIRED_Q;

/*
 * Wired callback queue
 * For callbacks that run with resource lock 0x0D held.
 * Original address: 0x00E2ADE0
 */
dxm_queue_t DXM_$WIRED_Q;

/*
 * Deferred signal-delivery routine table
 *
 * Original address: 0x00E85708 (Ghidra label DXM_$SIGNAL_ROUTINES;
 * NETLOG_$DATA_END sits at the same address).
 */
#if !defined(ARCH_M68K)
dxm_$signal_routine_t DXM_$SIGNAL_ROUTINES[DXM_SIGNAL_ROUTINE_COUNT] = {
    PROC2_$SIGNAL_OS,           /* 0xE85708: 0x00E3F0A6 */
    PROC2_$SIGNAL_PGROUP_OS,    /* 0xE8570C: 0x00E3F2C2 */
};
#endif

/*
 * ============================================================================
 * Callback-cell registry (host only)
 * ============================================================================
 *
 * A dxm_$callback_t is the 4-byte code address the image stores in a queue
 * entry.  A 64-bit host cannot put a function address in four bytes, so the
 * cell holds a 1-based handle into this table and dxm_$callback_fn() maps it
 * back.  See dxm/dxm.h (source-wy9y).
 */
#if !defined(ARCH_M68K)

static dxm_$callback_fn_t dxm_$host_callbacks[DXM_HOST_CALLBACK_MAX];
static uint32_t dxm_$host_callback_count;

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    uint32_t i;

    for (i = 0; i < dxm_$host_callback_count; i++) {
        if (dxm_$host_callbacks[i] == fn) {
            return (dxm_$callback_t)(i + 1);
        }
    }
    if (dxm_$host_callback_count >= DXM_HOST_CALLBACK_MAX) {
        return 0;
    }
    dxm_$host_callbacks[dxm_$host_callback_count] = fn;
    dxm_$host_callback_count++;
    return (dxm_$callback_t)dxm_$host_callback_count;
}

dxm_$callback_fn_t dxm_$callback_fn(dxm_$callback_t cell)
{
    if (cell == 0 || cell > dxm_$host_callback_count) {
        return NULL;
    }
    return dxm_$host_callbacks[cell - 1];
}

#endif /* !ARCH_M68K */

/*
 * Cell holding DXM_$ADD_SIGNAL_CALLBACK's address
 *
 * DXM_$ADD_SIGNAL pushes the ADDRESS of this cell
 * (`pea PTR_DXM_$ADD_SIGNAL_CALLBACK` at 0x00E172AA), so the cell itself
 * holds the 4-byte code address.
 *
 * Original address: 0x00E172CC
 */
DXM_$DEFINE_CALLBACK_CELL(PTR_DXM_$ADD_SIGNAL_CALLBACK, DXM_$ADD_SIGNAL_CALLBACK);
