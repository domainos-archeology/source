/*
 * EC Internal Header
 *
 * Internal data structures and globals for the EC subsystem.
 * This header should only be included by ec/ source files.
 */

#ifndef EC_INTERNAL_H
#define EC_INTERNAL_H

#include "ec.h"

/*
 * ============================================================================
 * Internal Global Data (EC2 subsystem state)
 * ============================================================================
 */

/*
 * EC2 Registration Table
 * Array of EC1 pointers indexed by registration ID.
 * Located at 0xE7CAF8 (m68k).
 */
extern void *DAT_00e7caf8[];

/*
 * Maximum registered EC1 index
 * Starts at 1, increments with each registration.
 * Located at 0xE7CF04 (m68k).
 */
extern uint32_t _DAT_00e7cf04;

/*
 * Pointer to max registered index
 * Points to _DAT_00e7cf04.
 * Located at 0xE7CAFC (m68k).
 */
extern void *DAT_00e7cafc;

/*
 * Registration count
 * Located at 0xE7CAF0 (m68k).
 */
extern uint16_t DAT_00e7caf0;

/*
 * Registration search counter
 * Used during EC1 registration lookup.
 * Located at 0xE7CF06 (m68k).
 */
extern uint16_t DAT_00e7cf06;

/*
 * EC2 free list head
 * Index of first free waiter entry.
 * Located at 0xE7CF08 (m68k).
 */
extern uint16_t DAT_00e7cf08;

/*
 * EC2 allocation bitmap
 * Tracks allocated PBU pool entries (bits 0-31).
 * Located at 0xE7CF00 (m68k).
 */
extern uint32_t DAT_00e7cf00;

/*
 * EC2 pending release bitmap
 * Tracks PBU entries awaiting release (reference count > 0).
 * Located at 0xE7CEFC (m68k).
 */
extern uint32_t DAT_00e7cefc;

/*
 * ============================================================================
 * External References (from other subsystems)
 * ============================================================================
 */

/*
 * Address space protection boundary
 * Addresses >= this value are protected from direct EC2 access.
 * TODO: Move to proc/as.h when that subsystem is cleaned up.
 */
extern void *AS_$PROTECTION;

#endif /* EC_INTERNAL_H */
