/*
 * MST Internal Header
 *
 * Internal data structures and globals for the MST subsystem.
 * This header should only be included by mst/ source files.
 */

#ifndef MST_INTERNAL_H
#define MST_INTERNAL_H

#include "mst.h"
#include "uid/uid.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "proc1/proc1.h"
#include "area/area.h"
#include "ast/ast.h"

/*
 * ============================================================================
 * CPU Type (from mmu)
 * ============================================================================
 */

/*
 * M68020 - CPU type flag
 * High bit set (negative value) indicates M68020 or later processor.
 * This is a 16-bit value in mmu.h.
 */
#define M68020 mmu_m68020_flag
extern int8_t mmu_m68020_flag;

/*
 * ============================================================================
 * Internal Global Data
 * ============================================================================
 */

/*
 * ASID allocation bitmap (32-bit access views)
 * MST_$ASID_LIST is also accessible as two 32-bit words.
 */
extern uint32_t MST_$ASID_LIST_LONG;    /* First 32 bits of ASID bitmap */
extern uint32_t DAT_00e24388;           /* Second part of ASID bitmap */

/*
 * MST page availability bitmap
 * Tracks which MST table pages are available for allocation.
 * Located at 0xE7CF0C (m68k).
 */
extern uint32_t DAT_00e7cf0c[];         /* Bitmap of available MST pages */
extern uint8_t DAT_00e7cf0f;            /* Flags byte in page bitmap */

/*
 * ============================================================================
 * Error Status (from pmap)
 * ============================================================================
 */

extern status_$t PMAP_VM_Resources_exhausted_err;

/*
 * ============================================================================
 * Error Status (internal)
 * ============================================================================
 */

extern status_$t MST_Ref_OutOfBounds_Err;

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * FUN_00e43f40 - Initialize segment table page for an ASID
 */
status_$t FUN_00e43f40(uint16_t asid, uint16_t flags, void *table_ptr);

/*
 * FUN_00e43182 - Internal mapping helper
 *
 * Returns the mapped virtual address in A0 register.
 */
void *FUN_00e43182(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                   uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                   uint8_t access_rights, int16_t direction, void *map_info,
                   status_$t *status);

/*
 * FUN_00e4411c - Internal get UID helper
 */
void FUN_00e4411c(uint16_t asid, uint32_t va, void *param, void **entry_out,
                  status_$t *status);

#endif /* MST_INTERNAL_H */
