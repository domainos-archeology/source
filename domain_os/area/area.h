/*
 * AREA - Area (Multi-Segment) Management
 *
 * This module provides area-level operations for Domain/OS virtual memory.
 * An "area" is a contiguous virtual address range that can span multiple
 * segments. Areas support:
 *   - Dynamic growth and shrinkage
 *   - Copy-on-write duplication
 *   - Remote (networked) backing storage
 *   - Association with AST (Address Space Table) entries
 *
 * Memory layout (m68k):
 *   - Area table base: 0xD94C00 (each entry 0x30 bytes)
 *   - Module globals: 0xE1E118
 *   - Maximum areas: 0x3A (58)
 *
 * Area IDs are calculated as: ((entry_ptr - 0xD94C00) / 0x30) + 1
 * An area handle combines generation (high word) and area ID (low word).
 *
 * Lock: ML_LOCK_AREA (0x0E) for area table operations
 *       ML_LOCK_AST (0x14) for AST operations within area functions
 */

#ifndef AREA_H
#define AREA_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * Lock ID for AREA operations
 */
#define ML_LOCK_AREA    0x0E

/*
 * Area table constants
 */
#define AREA_TABLE_BASE         0xD94C00
#define AREA_ENTRY_SIZE         0x30        /* 48 bytes per entry */
#define AREA_MAX_ENTRIES        0x3A        /* 58 entries */

/*
 * Area module globals base address
 */
#define AREA_GLOBALS_BASE       0xE1E118

/*
 * Area entry flags (in flags field at offset 0x2E)
 */
#define AREA_FLAG_ACTIVE        0x0001      /* Area is active/in-use */
#define AREA_FLAG_REVERSED      0x0002      /* Segment ordering is reversed */
#define AREA_FLAG_TOUCHED       0x0004      /* Area has been touched */
#define AREA_FLAG_SHARED        0x0008      /* Area is shared */
#define AREA_FLAG_IN_TRANS      0x0010      /* Area operation in progress */

/*
 * Status codes for AREA operations (module 0x32)
 */
#define status_$area_none_free          0x00320001  /* No free area entries */
#define status_$area_bad_handle         0x00320002  /* Invalid area handle */
#define status_$area_bad_offset         0x00320003  /* Invalid offset */
#define status_$area_create_failed      0x00320004  /* Create operation failed */
#define status_$area_no_uid             0x00320005  /* No UID available */
#define status_$area_not_active         0x00320006  /* Area not active */
#define status_$area_not_owner          0x00320007  /* Caller doesn't own area */
#define status_$area_not_found          0x00320008  /* Area not found */
#define status_$area_no_free_resources  0x00320005  /* No free resources */
#define status_$area_bad_reserve        0x0032000B  /* Invalid reserve size */

/*
 * Area entry structure
 * Size: 0x30 bytes (48 bytes)
 *
 * Areas are organized in per-ASID linked lists (via next/prev).
 * The seg_bitmap tracks which segments (0-63) have been allocated.
 * For areas with more than 16 segments, extended bitmap tables are used.
 */
typedef struct area_$entry_t {
    struct area_$entry_t *next;     /* 0x00: Next in per-ASID list */
    struct area_$entry_t *prev;     /* 0x04: Previous in per-ASID list */
    uint32_t virt_size;             /* 0x08: Virtual size (bytes, 32KB aligned) */
    uint32_t commit_size;           /* 0x0C: Committed/reserved size (bytes) */
    uint32_t caller_id;             /* 0x10: Caller-provided unique ID (for dedup) */
    int16_t first_bste;             /* 0x14: First BSTE index, -1 if unset */
    int16_t first_seg_index;        /* 0x16: First segment index in area */
    uint32_t seg_bitmap[2];         /* 0x18-0x1F: Segment allocation bitmap */
    uint32_t remote_uid;            /* 0x20: Remote UID for networked areas */
    int16_t volx;                   /* 0x24: Local volume index */
    int16_t owner_asid;             /* 0x26: Owner address space ID */
    int16_t remote_volx;            /* 0x28: Remote volume index */
    int16_t reserved_2a;            /* 0x2A: Reserved */
    int16_t generation;             /* 0x2C: Generation number (for handle validation) */
    uint16_t flags;                 /* 0x2E-0x2F: Flags (see AREA_FLAG_*) */
} area_$entry_t;

/*
 * Area handle type
 * High word: generation number
 * Low word: area ID (1-based index into area table)
 */
typedef uint32_t area_$handle_t;

/*
 * Extract area ID from handle
 */
#define AREA_HANDLE_TO_ID(h)    ((h) & 0xFFFF)

/*
 * Extract generation from handle
 */
#define AREA_HANDLE_TO_GEN(h)   ((h) >> 16)

/*
 * Build handle from generation and ID
 */
#define AREA_MAKE_HANDLE(gen, id)   (((uint32_t)(gen) << 16) | ((id) & 0xFFFF))

/*
 * Convert area ID to entry pointer
 */
#define AREA_ID_TO_ENTRY(id)    ((area_$entry_t *)(AREA_TABLE_BASE + ((id) - 1) * AREA_ENTRY_SIZE))

/*
 * Convert entry pointer to area ID
 */
#define AREA_ENTRY_TO_ID(ptr)   ((int16_t)(((uintptr_t)(ptr) - AREA_TABLE_BASE) / AREA_ENTRY_SIZE + 1))

/*
 * UID hash table entry for area deduplication
 * Used by AREA_$CREATE_FROM to find existing areas for the same remote UID
 */
typedef struct area_$uid_hash_t {
    struct area_$uid_hash_t *next;  /* 0x00: Next in hash chain */
    area_$entry_t *first_entry;     /* 0x04: First area entry with this UID */
} area_$uid_hash_t;

/*
 * ============================================================================
 * Module Global Variables (at AREA_GLOBALS_BASE + offset)
 * ============================================================================
 */

/*
 * AREA_$FREE_LIST - Head of free area entry list
 * Offset: +0x5C8 from AREA_GLOBALS_BASE
 */
extern area_$entry_t *AREA_$FREE_LIST;

/*
 * AREA_$N_FREE - Count of free area entries
 * Offset: +0x5E0 from AREA_GLOBALS_BASE
 */
extern int16_t AREA_$N_FREE;

/*
 * AREA_$N_AREAS - Highest area ID currently in use
 * Offset: +0x5E2 from AREA_GLOBALS_BASE
 */
extern int16_t AREA_$N_AREAS;

/*
 * AREA_$PARTNER_PKT_SIZE - Packet size for area partner operations
 * Offset: +0x5E4 from AREA_GLOBALS_BASE
 */
extern int16_t AREA_$PARTNER_PKT_SIZE;

/*
 * AREA_$PARTNER - Current area partner for network operations
 * Offset: +0x5CC from AREA_GLOBALS_BASE
 */
extern void *AREA_$PARTNER;

/*
 * AREA_$IN_TRANS_EC - Event count for area-in-transition waits
 * Offset: +0x48 from AREA_GLOBALS_BASE
 */
extern ec_$eventcount_t AREA_$IN_TRANS_EC;

/*
 * AREA_$CR_DUP - Count of duplicate area creates (dedup hits)
 * Offset: +0x5DE from AREA_GLOBALS_BASE
 */
extern int16_t AREA_$CR_DUP;

/*
 * AREA_$DEL_DUP - Count of duplicate area deletes
 * Offset: +0x5DC from AREA_GLOBALS_BASE
 */
extern int16_t AREA_$DEL_DUP;

/*
 * Per-ASID area list heads
 * Array at AREA_GLOBALS_BASE + 0x4D8, indexed by ASID
 * Each entry points to first area owned by that ASID
 */
#define AREA_ASID_LIST_BASE     (AREA_GLOBALS_BASE + 0x4D8)

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * AREA_$INIT - Initialize the area subsystem
 *
 * Called during system startup. Initializes:
 *   - Free list of area entries
 *   - Per-ASID area list heads
 *   - UID hash table for deduplication
 *   - Diskless node support structures
 *
 * Original address: 0x00E2F3A8
 */
void AREA_$INIT(void);

/*
 * AREA_$CREATE - Create a new area in current address space
 *
 * Creates a new area with the specified virtual and committed sizes.
 *
 * @param virt_size     Virtual size in bytes (rounded up to 32KB)
 * @param commit_size   Initial committed size in bytes (rounded up to 1KB)
 * @param shared        If negative (bit 7 set), area is shared
 * @param status_p      Output: status code
 *
 * Returns: Area handle (generation << 16 | area_id)
 *
 * Original address: 0x00E079C0
 */
void AREA_$CREATE(uint32_t virt_size, uint32_t commit_size,
                  int8_t shared, status_$t *status_p);

/*
 * AREA_$CREATE_FROM - Create area from remote UID (deduplicating)
 *
 * Creates a new area backed by a remote UID. If an area already exists
 * for the same UID and caller_id, returns a reference to the existing
 * area instead of creating a new one.
 *
 * @param remote_uid    Remote object UID
 * @param virt_size     Virtual size in bytes
 * @param commit_size   Committed size in bytes
 * @param caller_id     Caller-provided unique ID for dedup matching
 * @param status_p      Output: status code
 *
 * Returns: Area ID
 *
 * Original address: 0x00E07A02
 */
uint16_t AREA_$CREATE_FROM(uint32_t remote_uid, uint32_t virt_size,
                           uint32_t commit_size, int32_t caller_id,
                           int32_t *status_p);

/*
 * AREA_$DELETE - Delete an area
 *
 * Deletes the specified area, freeing all associated resources.
 * Caller must own the area.
 *
 * @param handle        Area handle
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E07C22
 */
void AREA_$DELETE(area_$handle_t handle, status_$t *status_ret);

/*
 * AREA_$DELETE_FROM - Delete area with specific caller context
 *
 * @param handle        Area handle
 * @param param_2       Unknown parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E07D06
 */
void AREA_$DELETE_FROM(area_$handle_t handle, uint32_t param_2,
                       status_$t *status_ret);

/*
 * AREA_$FREE_ASID - Free all areas owned by an address space
 *
 * Called when an address space is being destroyed.
 * Frees all areas owned by the specified ASID.
 *
 * @param asid          Address space ID
 *
 * Original address: 0x00E07E80
 */
void AREA_$FREE_ASID(int16_t asid);

/*
 * AREA_$SHUTDOWN - Shutdown area subsystem
 *
 * Called during system shutdown to release area resources.
 *
 * Original address: 0x00E07F0E
 */
void AREA_$SHUTDOWN(void);

/*
 * AREA_$FREE_FROM - Free areas from specific context
 *
 * @param param_1       Unknown parameter
 *
 * Original address: 0x00E07FC6
 */
void AREA_$FREE_FROM(uint32_t param_1);

/*
 * AREA_$TRANSFER - Transfer area ownership to another address space
 *
 * Transfers ownership of an area to a new address space.
 * Also updates the area's virtual size.
 *
 * @param handle_ptr    Pointer to area handle
 * @param new_asid      New owner ASID
 * @param new_seg_idx   New segment index
 * @param new_virt_size New virtual size
 * @param status_ret    Output: status code
 *
 * Returns: Previous segment index
 *
 * Original address: 0x00E08098
 */
int16_t AREA_$TRANSFER(area_$handle_t *handle_ptr, int16_t new_asid,
                       int16_t new_seg_idx, uint32_t new_virt_size,
                       status_$t *status_ret);

/*
 * AREA_$GROW - Grow an area's virtual size
 *
 * Increases the virtual size of an area. May allocate additional
 * segments and backing store.
 *
 * @param gen           Area generation
 * @param area_id       Area ID
 * @param virt_size     New virtual size
 * @param commit_size   New committed size
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E08BE8
 */
void AREA_$GROW(int16_t gen, uint16_t area_id, uint32_t virt_size,
                uint32_t commit_size, status_$t *status_ret);

/*
 * AREA_$GROW_TO - Grow area to specified size
 *
 * @param gen           Area generation
 * @param area_id       Area ID
 * @param virt_size     Target virtual size
 * @param commit_size   Target committed size
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E08CEA
 */
void AREA_$GROW_TO(int16_t gen, uint16_t area_id, uint32_t virt_size,
                   uint32_t commit_size, status_$t *status_ret);

/*
 * AREA_$INVALIDATE - Invalidate area pages
 *
 * Invalidates pages within the specified range of an area.
 * This is used to discard pages that are no longer needed.
 *
 * For normal (non-reversed) areas, pages are invalidated from
 * the specified offset forward.
 *
 * For reversed areas (stack-like), the invalidation logic is
 * adjusted to handle the reversed page ordering.
 *
 * Parameters:
 *   gen         - Area generation
 *   area_id     - Area ID
 *   seg_idx     - Segment index
 *   page_offset - Page offset within segment
 *   count       - Number of pages to invalidate
 *   param_6     - Unknown parameter (unused?)
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E08DD0
 */
void AREA_$INVALIDATE(int16_t gen, uint16_t area_id, uint16_t seg_idx,
                      uint16_t page_offset, uint32_t count,
                      int16_t param_6, status_$t *status_ret);

/*
 * AREA_$COPY - Copy an area (copy-on-write)
 *
 * Creates a copy of an area with copy-on-write semantics.
 * Both source and destination share pages until written.
 *
 * @param gen           Source area generation
 * @param area_id       Source area ID
 * @param new_asid      New owner ASID for copy
 * @param param_4       Unknown parameter
 * @param stack_limit   Stack limit for copy
 * @param status_ret    Output: status code
 *
 * Returns: New area ID
 *
 * Original address: 0x00E0901A
 */
uint32_t AREA_$COPY(int16_t gen, uint16_t area_id, int16_t new_asid,
                    int16_t param_4, uint32_t stack_limit,
                    status_$t *status_ret);

/*
 * AREA_$TOUCH - Touch area pages (bring into memory)
 *
 * Ensures pages within the specified range are in memory.
 *
 * @param handle_ptr    Pointer to area handle
 * @param bste_idx      BSTE index
 * @param seg_idx       Segment index
 * @param param_4       Unknown parameter
 * @param param_5       Unknown parameter
 * @param status_p      Output: status code
 *
 * Original address: 0x00E094FE
 */
void AREA_$TOUCH(area_$handle_t *handle_ptr, uint16_t bste_idx,
                 uint16_t seg_idx, int16_t param_4, uint32_t param_5,
                 status_$t *status_p);

/*
 * AREA_$ASSOC - Associate area with AST entry
 *
 * Associates an area with an Address Space Table entry.
 *
 * @param gen           Area generation
 * @param area_id       Area ID
 * @param aste_idx      AST entry index
 * @param param_4       Unknown parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E096A2
 */
void AREA_$ASSOC(uint16_t gen, int16_t area_id, uint32_t aste_idx,
                 int16_t param_4, status_$t *status_ret);

/*
 * AREA_$THREAD_BSTES - Thread BSTE entries for area
 *
 * Links BSTE (Backing Store Table Entry) entries for the area.
 *
 * @param handle_ptr    Pointer to area handle
 * @param bste_idx      First BSTE index
 * @param seg_idx       First segment index
 * @param param_4       Unknown parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E09722
 */
void AREA_$THREAD_BSTES(area_$handle_t *handle_ptr, int16_t bste_idx,
                        int16_t seg_idx, uint32_t param_4,
                        status_$t *status_ret);

/*
 * AREA_$REMOVE_SEG - Remove segment from area
 *
 * Removes a segment from the area's segment list.
 *
 * @param area_id       Area ID
 * @param seg_idx       Segment index to remove
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E09822
 */
void AREA_$REMOVE_SEG(uint16_t area_id, uint16_t seg_idx,
                      status_$t *status_ret);

/*
 * AREA_$DEACTIVATE_ASTE - Deactivate AST entry for area
 *
 * @param area_id       Area ID
 * @param param_2       Unknown parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E09EF4
 */
void AREA_$DEACTIVATE_ASTE(uint16_t area_id, uint32_t param_2,
                           status_$t *status_ret);

/*
 * ============================================================================
 * Internal Functions (declared for cross-module use)
 * ============================================================================
 */

/*
 * area_$wait_in_trans - Wait for area in-transition to complete
 *
 * Called when AREA_FLAG_IN_TRANS is set to wait for the
 * current operation to complete.
 *
 * Original address: 0x00E07742
 */
void area_$wait_in_trans(void);

/*
 * area_$internal_delete - Internal area deletion
 *
 * Core deletion logic used by public deletion functions.
 *
 * @param entry         Area entry pointer
 * @param area_id       Area ID
 * @param status_p      Output: status code
 * @param do_unlink     If negative, unlink from ASID list
 *
 * Original address: 0x00E07B50
 */
void area_$internal_delete(area_$entry_t *entry, int16_t area_id,
                           status_$t *status_p, int8_t do_unlink);

/*
 * area_$internal_create - Internal area creation
 *
 * Core creation logic used by public creation functions.
 *
 * @param virt_size     Virtual size
 * @param commit_size   Committed size
 * @param remote_uid    Remote UID (0 for local)
 * @param owner_asid    Owner ASID
 * @param alloc_remote  Allocate remote backing
 * @param shared        Shared flag
 * @param status_p      Output: status code
 *
 * Returns: Area handle
 *
 * Original address: 0x00E077DA
 */
uint32_t area_$internal_create(uint32_t virt_size, uint32_t commit_size,
                               uint32_t remote_uid, int16_t owner_asid,
                               int16_t alloc_remote, int8_t shared,
                               status_$t *status_p);

/*
 * area_$resize - Resize an area
 *
 * Core resize logic for grow/shrink operations.
 *
 * @param area_id       Area ID
 * @param entry         Area entry pointer
 * @param virt_size     New virtual size
 * @param commit_size   New committed size
 * @param is_grow       1 for grow, 0 for initial create
 * @param status_p      Output: status code
 *
 * Original address: 0x00E08816
 */
void area_$resize(int16_t area_id, area_$entry_t *entry,
                  uint32_t virt_size, uint32_t commit_size,
                  int16_t is_grow, status_$t *status_p);

#endif /* AREA_H */
