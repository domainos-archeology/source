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
 * DXM_$SIGNAL_ROUTINES - the deferred signal-delivery routine table, the
 * whole DXM_WIRED_ data segment (map "D E85708 DXM_WIRED_ size = 8"; Ghidra
 * label DXM_$SIGNAL_ROUTINES, NETLOG_$DATA_END sits at the same address).
 * Image bytes (`gsk read 0xE85708 8`): 00 e3 f0 a6  00 e3 f2 c2.
 */
MODULE_DATA_DEFINE_INIT(dxm_$signal_routines_t, DXM_$SIGNAL_ROUTINES, 0x00E85708, {
    PROC2_$SIGNAL_OS,           /* 0xE85708: 0x00E3F0A6 */
    PROC2_$SIGNAL_PGROUP_OS,    /* 0xE8570C: 0x00E3F2C2 */
});

/*
 * Cell holding DXM_$ADD_SIGNAL_CALLBACK's address
 *
 * DXM_$ADD_SIGNAL pushes the ADDRESS of this cell ("pea (0x14,PC)" at
 * 0x00E172B6; 0x00E172B8 + 0x14 = 0x00E172CC), so the cell itself holds the
 * 4-byte code address.  Image bytes:
 *
 *   00e172c8  4e 5e 4e 75 00 e7 21 84
 *                         ^^^^^^^^^^^  = 0x00E72184, DXM_$ADD_SIGNAL_CALLBACK
 *
 * The cell is the last longword of the DXM_WIRED_ code segment (SAU2 map:
 * "I E16FE0 DXM_WIRED_ size = 2F0", i.e. 0xE16FE0..0xE172CF) and has no
 * symbol of its own there, so the descriptive name is the tree's.
 *
 * Original address: 0x00E172CC
 */
DXM_$DEFINE_CALLBACK_CELL(DXM_$ADD_SIGNAL_CALLBACK_CELL, DXM_$ADD_SIGNAL_CALLBACK);
