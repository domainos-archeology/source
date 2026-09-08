/*
 * area_data.c - AREA Module Global Data Definitions
 *
 * This file defines the global variables used by the AREA (Multi-Segment
 * Area Management) module. Areas are contiguous virtual address ranges
 * that can span multiple segments, supporting dynamic growth, copy-on-write
 * duplication, and remote (networked) backing storage.
 *
 * All variables are initialized to zero/NULL here; runtime initialization
 * occurs in AREA_$INIT (0x00E2F3A8).
 *
 * Original M68K addresses:
 *   AREA_$IN_TRANS_EC:       0xE1E160 (12 bytes) - In-transition eventcount
 *   AREA_$FREE_LIST:         0xE1E6E0 (4 bytes)  - Head of free entry list
 *   AREA_$PARTNER:           0xE1E6E4 (4 bytes)  - Network partner pointer
 *   AREA_$DEL_DUP:           0xE1E6F4 (2 bytes)  - Duplicate delete count
 *   AREA_$CR_DUP:            0xE1E6F6 (2 bytes)  - Duplicate create count
 *   AREA_$N_FREE:            0xE1E6F8 (2 bytes)  - Free entry count
 *   AREA_$N_AREAS:           0xE1E6FA (2 bytes)  - Highest area ID in use
 *   AREA_$PARTNER_PKT_SIZE:  0xE1E6FC (2 bytes)  - Partner packet size
 */

#include "area/area_internal.h"

/*
 * ============================================================================
 * The AREA_ module data block
 * ============================================================================
 *
 * Every AREA_ routine begins `lea (0xe1e118).l,A5`, so this is a single
 * object, not a collection of independent globals; area/area.h defines the
 * record and aliases each map-named cell onto its field.  The SR10.2 SAU2
 * link map fixes the extent:
 *
 *   D    E1E118  AREA_    size = 5E8
 *
 * which area/area.h asserts with `sizeof(area_$globals_t) == 0x5E8`.
 * Everything is zero at load; AREA_$INIT (0x00E2F3A8) fills it in.
 *
 * The interior symbols the map names - AREA_$RPMAP_IN_TRANS_EC (+0x038),
 * AREA_$IN_TRANS_EC (+0x048), AREA_$PITE_IN_TRANS_EC (+0x058),
 * AREA_$FREE_LIST (+0x5C8), AREA_$PARTNER (+0x5CC), AREA_$FORMAT (+0x5D4),
 * AREA_$DEL_DUP (+0x5DC), AREA_$CR_DUP (+0x5DE), AREA_$N_FREE (+0x5E0),
 * AREA_$N_AREAS (+0x5E2) and AREA_$PARTNER_PKT_SIZE (+0x5E4) - are reachable
 * under those names through the aliases in area/area.h.
 *
 * (source-vm49: this replaces eleven loose objects that the m68k link would
 * have scattered, together with the raw `(uint32_t *)AREA_GLOBALS_BASE`
 * pointer arithmetic AREA_$INIT used to reach the rest of the block.)
 *
 * Original address: 0xE1E118
 */
area_$globals_t AREA_$GLOBALS;

/*
 * ============================================================================
 * In-code status constant
 * ============================================================================
 */

/*
 * Area_Internal_Error - the status AREA_ hands CRASH_SYSTEM when it finds its
 * own tables inconsistent.
 *
 * The cell is a literal inside the AREA_ code segment (it sits between
 * AREA_$DELETE_FROM at 0xE07D06 and AREA_$FREE_ASID at 0xE07E80, so the map
 * gives it no symbol of its own); thirteen `pea (d,PC)` sites in AREA_ point
 * at it.  Image bytes at 0x00E07E7C: 00 32 00 0a.
 *
 * Original address: 0xE07E7C
 */
status_$t Area_Internal_Error = 0x0032000A;
