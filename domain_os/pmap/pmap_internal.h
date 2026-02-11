/*
 * pmap/pmap_internal.h - Internal PMAP Definitions
 *
 * Contains internal functions, data, and types used only within
 * the pmap (physical map) subsystem. External consumers should use pmap/pmap.h.
 */

#ifndef PMAP_INTERNAL_H
#define PMAP_INTERNAL_H

#include "pmap/pmap.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "proc1/proc1.h"
#include "network/network.h"
#include "disk/disk.h"

/*
 * ============================================================================
 * Internal Configuration
 * ============================================================================
 */

/* Page thresholds for purifier decisions */
extern uint16_t PMAP_$LOW_THRESH;       /* Low page threshold */
extern uint16_t PMAP_$MID_THRESH;       /* Middle page threshold */
extern uint16_t PMAP_$WS_SCAN_DELTA;    /* Working set scan delta */
extern uint16_t PMAP_$MAX_WS_INTERVAL;  /* Max working set interval */
extern uint16_t PMAP_$MIN_WS_INTERVAL;  /* Min working set interval */
extern uint32_t PMAP_$IDLE_INTERVAL;    /* Idle interval */
extern uint32_t PMAP_$PUR_L_CNT;        /* Local purifier page count */
extern uint32_t PMAP_$PUR_R_CNT;        /* Remote purifier page count */

/* Shutdown flag */
extern int8_t PMAP_$SHUTTING_DOWN_FLAG;

/*
 * ============================================================================
 * Working Set Data (at fixed addresses on m68k)
 * ============================================================================
 */

/* Working set page counts (0xE232B4 region) */
extern uint32_t DAT_00e232b4;   /* Working set 0 page count */
extern uint32_t DAT_00e232d8;   /* Working set 1 page count */
extern uint32_t DAT_00e232fc;   /* Working set 2 page count */
extern uint32_t DAT_00e23320;   /* Free page count */
extern uint32_t DAT_00e23344;   /* Remote page count */

/* Global scan data */
extern uint32_t DAT_00e23380;   /* Last global scan time */
extern uint32_t DAT_00e2337c;   /* Previous global scan time */
extern uint16_t DAT_00e23366;   /* Global scan counter */
extern uint32_t DAT_00e2336c;   /* Global scan data */
extern uint32_t DAT_00e23368;   /* Global scan source */

/* Timer purifier data */
extern uint16_t DAT_00e254e4;   /* Current scan slot (5-69) */
extern uint16_t DAT_00e254e2;   /* Random seed for page selection */
extern uint32_t DAT_00e254dc;   /* Wait eventcount */
extern uint32_t DAT_00e1416a;   /* Short wait time */

/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * pmap_$flush_write_batch - Batch write dirty pages to disk
 *
 * Nested Pascal procedure that accesses parent frame.
 * Unlocks lock 14, allocates disk queue blocks, fills them
 * with write requests, calls DISK_$WRITE_MULTI, processes
 * results, and advances AST_$PMAP_IN_TRANS_EC.
 *
 * Original address: 0x00e1360c
 */
void pmap_$flush_write_batch(void);

/* pmap_$update_seg_map - Update segment map after page write
 *
 * Checks ASTE flag at offset 0x15 bit 0. If not set, calls
 * MMAP_$AVAIL. If set, calls AST_$INVALIDATE_PAGE and optionally
 * logs via NETLOG_$LOG_IT.
 *
 * NOTE: Uses hidden A1 register parameter (ASTE pointer) from
 * the m68k calling convention. On m68k, A1 is set by the caller
 * and read via movea.l A1,A2 at function entry.
 *
 * Original address: 0x00e1359c
 * Size: 112 bytes
 */
void pmap_$update_seg_map(uint16_t *segmap_entry, uint32_t vpn, uint16_t page_idx);

/*
 * pmap_$write_page - Write a single page to disk or network
 *
 * Handles writing a page to either local disk or remote network
 * node depending on whether the page belongs to a network-mapped
 * object. Manages checksums, logging, error handling, and
 * page map invalidation on failure.
 *
 * Original address: 0x00e12e5e
 */
void pmap_$write_page(uint32_t vpn, status_$t *status, int8_t sync_flag);

/*
 * FUN_00e12d38 - Cleanup helper
 * Original address: 0x00e12d38
 */
void FUN_00e12d38(void);

/*
 * FUN_00e1327e - Page selection helper
 * Original address: 0x00e1327e
 */
void FUN_00e1327e(int *pages, int qblk, uint16_t count);

/* pmap_$write_complete - Page write I/O completion handler
 *
 * Handles completion of a page write operation. Indexes into the
 * page frame table at 0xEB4800 (offset = vpn * 0x10). On success
 * (status 0 or write-protected): clears status, updates dirty flags,
 * clears physical map in-transit bit, updates page state. On error:
 * sets error bit, marks page, updates hardware PTE, calls MMAP_$AVAIL,
 * advances AST_$PMAP_IN_TRANS_EC.
 *
 * Original address: 0x00e12d84
 * Size: 218 bytes
 */
void pmap_$write_complete(int32_t vpn, void *status_ptr);

/*
 * FUN_00e2f880 - Unknown helper
 * Original address: 0x00e2f880
 */
void FUN_00e2f880(void);

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */

/* status_$t_00e13a14 and status_$t_00e145ec declared in pmap.h */

/*
 * ============================================================================
 * External Module Dependencies
 * ============================================================================
 */

/* Network/logging flags */
extern int8_t NETLOG_$OK_TO_LOG;
extern uint32_t *LOG_$LOGFILE_PTR;
int32_t LOG_$UPDATE(void);

#endif /* PMAP_INTERNAL_H */
