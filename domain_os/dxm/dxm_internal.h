/*
 * dxm/dxm_internal.h - Deferred Execution Manager Internal API
 *
 * Internal functions and data used within the DXM subsystem.
 */

#ifndef DXM_INTERNAL_H
#define DXM_INTERNAL_H

#include "dxm/dxm.h"
#include "proc2/proc2.h"
#include "proc1/proc1.h"
#include "misc/misc.h"
#include "misc/crash_system.h"
#include "ml/ml.h"

/*
 * The crash-console string at 0x00E17154 and the status constant at
 * 0x00E17164 are `pea (d16,PC)` cells inside DXM_$ADD_CALLBACK's own code
 * region, so they live as file-statics in dxm/add_callback.c.
 */

/*
 * Pointer to DXM_$ADD_SIGNAL_CALLBACK
 * Used as callback address when adding signal callbacks.
 * Original address: 0x00E172CC
 */
extern void (*PTR_DXM_$ADD_SIGNAL_CALLBACK)(void *);

#endif /* DXM_INTERNAL_H */
