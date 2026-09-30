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
#include "misc/crash_system.h"

/*
 * LITES_LOC - Display-memory address of the status lights (0 = disabled).
 *
 * Original address: 0x00E2327C (4 bytes)
 */
extern int32_t LITES_LOC;

/*
 * The other two words of the MEM_LITES data segment (map "D E2327C
 * MEM_LITES size = 8", whose only symbol is LITES_LOC): the display rows
 * the two light rows are drawn at, set by START_MEM_LITES from the visible
 * display height (A5 = 0xE2327C) and read by MEM_LITES.  Module-local, no
 * map symbol.
 *   0xE23280  +4  mem_lites_row2   height - 0x10  (0x00E0C472)
 *   0xE23282  +6  mem_lites_row1   height - 0x30  (0x00E0C466)
 */
extern uint16_t mem_lites_row2;
extern uint16_t mem_lites_row1;

/*
 * MEM_LITES - the status-lights process body (0x00E0C39C, 158 bytes),
 * started by START_MEM_LITES through PROC1_$CREATE_P.
 */
void MEM_LITES(void);

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
