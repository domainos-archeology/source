/*
 * dxm/dxm_internal.h - Deferred Execution Manager Internal API
 *
 * Internal functions and data used within the DXM subsystem.
 */

#ifndef DXM_INTERNAL_H
#define DXM_INTERNAL_H

#include "dxm/dxm.h"
#include "proc1/proc1.h"
#include "misc/misc.h"
#include "misc/crash_system.h"
#include "ml/ml.h"

/*
 * Error message for datum too large
 * Original address: 0x00E17162
 */
extern const char DXM_Datum_too_large_err[];

/*
 * Error message for queue full
 * Original address: 0x00E17154
 */
extern const char DXM_No_room_err[];

/*
 * Pointer to DXM_$ADD_SIGNAL_CALLBACK
 * Used as callback address when adding signal callbacks.
 * Original address: 0x00E172CC
 */
extern void (*PTR_DXM_$ADD_SIGNAL_CALLBACK)(void *);

#endif /* DXM_INTERNAL_H */
