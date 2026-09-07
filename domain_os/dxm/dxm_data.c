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
 * Pointer to signal callback function
 * Original address: 0x00E172CC
 */
void (*PTR_DXM_$ADD_SIGNAL_CALLBACK)(void *) = DXM_$ADD_SIGNAL_CALLBACK;
