/*
 * misc/misc_internal.h - Internal definitions for the misc "subsystem"
 *
 * Functions without a <NAMESPACE>_$ prefix live under misc/.  This header
 * holds declarations used only within misc/.  External consumers should
 * use misc/misc.h.
 */

#ifndef MISC_INTERNAL_H
#define MISC_INTERNAL_H

#include "misc/misc.h"

/*
 * LITES_LOC - Display-memory address of the status lights (0 = disabled).
 *
 * Original address: 0x00E2327C (4 bytes)
 */
extern int32_t LITES_LOC;

/*
 * START_MEM_LITES - Start the memory lights update process
 *
 * Creates a process that periodically updates the status lights to
 * reflect memory and system activity.  Not yet decompiled.
 *
 * Original address: 0x00E0C43C
 */
void START_MEM_LITES(void);

#endif /* MISC_INTERNAL_H */
