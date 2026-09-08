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
 * Cell holding DXM_$ADD_SIGNAL_CALLBACK's address (0x00E72184)
 *
 * Used as the callback address when adding signal callbacks; DXM_$ADD_SIGNAL
 * pushes its ADDRESS ("pea (0x14,PC)" at 0x00E172B6).  See dxm/dxm_data.c for
 * the image bytes.
 *
 * Original address: 0x00E172CC
 */
extern dxm_$callback_t DXM_$ADD_SIGNAL_CALLBACK_CELL;

#endif /* DXM_INTERNAL_H */
