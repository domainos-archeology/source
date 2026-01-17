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
 * Error messages
 */
const char DXM_Datum_too_large_err[] = "DXM: Datum too large";
const char DXM_No_room_err[] = " DXM: No room %H";

/*
 * Pointer to signal callback function
 * Original address: 0x00E172CC
 */
void (*PTR_DXM_$ADD_SIGNAL_CALLBACK)(void *) = DXM_$ADD_SIGNAL_CALLBACK;
