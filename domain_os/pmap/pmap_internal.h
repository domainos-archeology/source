/*
 * pmap/pmap_internal.h - Internal PMAP Definitions
 *
 * Contains internal functions, data, and types used only within
 * the pmap (physical map) subsystem. External consumers should use pmap/pmap.h.
 */

#ifndef PMAP_INTERNAL_H
#define PMAP_INTERNAL_H

#include "pmap/pmap.h"
#include "uid/uid.h"   /* UID_$NIL */
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "proc1/proc1.h"
#include "network/network.h"
#include "disk/disk.h"
#include "log/log.h"
#include "netlog/netlog.h"
#include "anon/anon.h"

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

/*
 * Global page-pool page counts.  These are not standalone variables: each
 * one is MMAP_$WSL[pool].page_count, i.e. 0xE232B0 + pool*0x24 + 4.  New
 * code should use MMAP_WSL[MMAP_WSL_POOL_*].page_count (mmap/mmap.h); the
 * DAT_ names below remain for the pmap/ast files that have not been
 * re-emitted yet.  Ghidra labels renamed to MMAP_$WSL_*_CNT.
 */
extern uint32_t DAT_00e232b4;   /* MMAP_$WSL[0].page_count - free pages */
extern uint32_t DAT_00e232d8;   /* MMAP_$WSL[1].page_count - pure pages */
extern uint32_t DAT_00e232fc;   /* MMAP_$WSL[2].page_count - clean impure */
extern uint32_t DAT_00e23320;   /* MMAP_$WSL[3].page_count - dirty, local */
extern uint32_t DAT_00e23344;   /* MMAP_$WSL[4].page_count - dirty, remote */

/* Global scan data */
extern uint32_t DAT_00e23380;   /* Last global scan time */
extern uint32_t DAT_00e2337c;   /* Previous global scan time */
extern uint16_t DAT_00e23366;   /* Global scan counter */
extern uint32_t DAT_00e2336c;   /* Global scan data */
extern uint32_t DAT_00e23368;   /* MMAP_$WSL[5].page_count - wired pages */

/* Timer purifier data */
extern uint16_t DAT_00e254e4;   /* Current scan slot (5-69) */
extern uint16_t DAT_00e254e2;   /* Random seed for page selection
                                 * (Ghidra: PMAP_$WS_RANDOM_SEED) */

/*
 * ============================================================================
 * Segment map (0xED4F80)
 * ============================================================================
 *
 * One 4-byte entry per page of a segment, 32 pages (0x80 bytes) per
 * segment.  PMAP_$PURIFIER_L reaches an entry with
 *   lea 0xED5000 + seg*0x80 + page*4, A0 ; bset.b #7,(-0x80,A0)
 * (0x00E13BEA-0x00E13C32), i.e. the array base is 0xED4F80 and the segment
 * index is 1-based, exactly as in pmap_$fill_write_qblks.
 */
typedef struct pmap_segmap_entry_t {
    uint8_t  flags;         /* 0x00: bit 7 = write in progress */
    uint8_t  reserved[3];   /* 0x01: rest of the 4-byte entry */
} pmap_segmap_entry_t;

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(pmap_segmap_entry_t, flags) == 0x00, "pmap_segmap_entry_t.flags");
_Static_assert(__builtin_offsetof(pmap_segmap_entry_t, reserved) == 0x01, "pmap_segmap_entry_t.reserved");

#define PMAP_SEGMAP_WRITING     0x80    /* bset.b #7 at 0x00E13C32 */
#define PMAP_SEGMAP_PAGES_PER_SEG 32    /* 0x80 bytes / 4 bytes per entry */

typedef pmap_segmap_entry_t pmap_segmap_row_t[PMAP_SEGMAP_PAGES_PER_SEG];

#if defined(ARCH_M68K)
_Static_assert(sizeof(pmap_segmap_entry_t) == 4, "segmap entry is 4 bytes");
_Static_assert(sizeof(pmap_segmap_row_t) == 0x80, "segmap row is 0x80 bytes");
#endif

#if defined(ARCH_M68K)
/* 1-based: PMAP_SEGMAP[seg][page] == 0xED4F80 + seg*0x80 + page*4 */
#define PMAP_SEGMAP ((pmap_segmap_row_t *)0xED4F80)
#else
extern pmap_segmap_row_t *pmap_segmap;
#define PMAP_SEGMAP pmap_segmap
#endif

/*
 * ============================================================================
 * Disk queue block, as used by the purifier write path
 * ============================================================================
 *
 * DISK_$GET_QBLKS hands back a chain of these; DISK_$WRITE_MULTI fills in
 * the per-page status.  Only the fields PMAP_$PURIFIER_L touches are named
 * (0x00E13D28-0x00E13D78 and 0x00E13D96-0x00E13DA0).
 */
typedef struct pmap_qblk_t {
    uint8_t  reserved_00[0x08]; /* 0x00: allocation chain + DISK private */
    struct pmap_qblk_t *next;   /* 0x08: next block of the result chain */
    status_$t status;           /* 0x0C: per-page write status */
    uint8_t  reserved_10[0x04]; /* 0x10 */
    uint32_t vpn;               /* 0x14: page that was written */
    uint8_t  reserved_18[0x24]; /* 0x18 */
    uint32_t log_info;          /* 0x3C: word pair logged by NETLOG_$LOG_IT */
} pmap_qblk_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(pmap_qblk_t, reserved_00) == 0x00, "pmap_qblk_t.reserved_00");
_Static_assert(__builtin_offsetof(pmap_qblk_t, reserved_10) == 0x10, "pmap_qblk_t.reserved_10");
_Static_assert(__builtin_offsetof(pmap_qblk_t, reserved_18) == 0x18, "pmap_qblk_t.reserved_18");
#endif

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(pmap_qblk_t, next) == 0x08, "pmap_qblk_t.next");
_Static_assert(__builtin_offsetof(pmap_qblk_t, status) == 0x0C, "pmap_qblk_t.status");
_Static_assert(__builtin_offsetof(pmap_qblk_t, vpn) == 0x14, "pmap_qblk_t.vpn");
_Static_assert(__builtin_offsetof(pmap_qblk_t, log_info) == 0x3C,
               "pmap_qblk_t.log_info");
#endif

/*
 * PMAP_$SHORT_WAIT_DELAY - relative delay used by PMAP_$PURIFIER_L
 * between working-set scans (TIME_$WAIT with a relative delay type).
 *
 * Original address: 0xE254DC (DAT_00e254dc), value 0x00000000:0005
 */
extern clock_t PMAP_$SHORT_WAIT_DELAY;

/*
 * Per-working-set timer queues and queue elements used by
 * PMAP_$INIT_WS_SCAN.  One slot per working set (0..69; slot 5 is
 * special and never gets a timer).
 *
 * Original addresses:
 *   PMAP_$WS_TIMER_QUEUES:   0xE2A494 (70 * 0x0C bytes)
 *   PMAP_$WS_TIMER_ELEMENTS: 0xE24D68 (70 * 0x1A bytes, DAT_00e24d68,
 *                            ends at PMAP_$IDLE_INTERVAL 0xE25484)
 */
#define PMAP_WS_SLOTS   70
extern time_queue_t      PMAP_$WS_TIMER_QUEUES[PMAP_WS_SLOTS];
extern time_queue_elem_t PMAP_$WS_TIMER_ELEMENTS[PMAP_WS_SLOTS];

/*
 * Timer queue elements for the update and purifier timers
 * (PMAP_$INIT_TIMERS).  On m68k these are at fixed addresses
 * (DAT_00e24d44 update, DAT_00e24d64 purifier).
 */
#if !defined(ARCH_M68K)
extern time_queue_elem_t pmap_update_timer_elem;    /* 0xE24D44 */
extern time_queue_elem_t pmap_purifier_timer_elem;  /* 0xE24D64 */
#endif

/*
 * Raw table base addresses used via pointer arithmetic by the purifier
 * and page-write code.  On m68k these are fixed addresses (see the
 * #define blocks in the .c files); other targets get them from platform
 * init.
 */
#if !defined(ARCH_M68K)
extern uint8_t wsl_base[];              /* 0xE232B0: working set list */
extern uint8_t segmap_base[];           /* 0xED5000: segment map */
extern uint8_t aote_table[];            /* 0xEC53F0: AOTE table */
extern uint8_t pur_stats[];             /* 0xE25D18: purifier statistics */
extern uint8_t *aote_table_ptr_base;    /* 0xEC53F0: AOTE pointer table */
extern uint8_t *segmap_indexed_base;    /* 0xED4F80: indexed segment map */
extern uint8_t *mmape_raw_base;         /* 0xEB2800: MMAPE array */
#endif

/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * pmap_$flush_write_batch - Batch write dirty pages to disk
 *
 * Nested Pascal procedure from PMAP_$FLUSH, flattened with explicit
 * parameters (originally accessed parent frame via A6 chain).
 * Unlocks lock 14, allocates disk queue blocks, fills them
 * with write requests, calls DISK_$WRITE_MULTI, processes
 * results, and advances AST_$PMAP_IN_TRANS_EC.
 *
 * Parameters:
 *   batch_count_p - Pointer to batch count (cleared to 0 on return)
 *   batch_vpns    - Array of VPNs to write
 *   segmap        - Segment map base pointer
 *   status        - Pointer to caller's status output
 *
 * Original address: 0x00e1360c
 */
void pmap_$flush_write_batch(int16_t *batch_count_p, uint32_t *batch_vpns,
                              uint32_t *segmap, status_$t *status);

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
 * pmap_$wait_in_transit - Wait for PMAP in-transit event count
 *
 * Waits for the AST_$PMAP_IN_TRANS_EC event count to advance past
 * its current value. Unlocks PMAP lock while waiting, re-acquires
 * it before returning. Used when pages are in transit to disk.
 *
 * Original address: 0x00e12d38
 * Size: 70 bytes
 */
void pmap_$wait_in_transit(void);

/*
 * pmap_$fill_write_qblks - Fill disk queue blocks with write descriptors
 *
 * Populates a linked list of disk queue blocks with the information
 * needed to write dirty pages to disk. Handles disk address allocation
 * via BAT_$ALLOCATE for pages without existing disk addresses.
 *
 * Original address: 0x00e1327e
 * Size: 798 bytes
 */
void pmap_$fill_write_qblks(int32_t *pages, uint32_t *qblk, int16_t count);

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
 * PMAP_$INIT_TIMERS - Initialize PMAP purifier and update timers
 *
 * Sets up two periodic timer callbacks in the real-time event queue:
 * - Purifier timer (PMAP_$T_PURIF_CALLBACK): fires every ~29 seconds
 * - Update timer (PMAP_$UPDATE_CALLBACK): fires every ~0.9ms
 *
 * Original address: 0x00e2f880
 * Size: 216 bytes
 */
void PMAP_$INIT_TIMERS(void);

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
 *
 * NETLOG_$OK_TO_LOG   - netlog/netlog.h
 * LOG_$LOGFILE_PTR,
 * LOG_$UPDATE         - log/log.h
 * DISK_$DO_CHKSUM     - disk/disk.h
 * NETWORK_$DISKLESS   - network/network.h
 * ANON_$UID           - anon/anon.h
 */

#endif /* PMAP_INTERNAL_H */
