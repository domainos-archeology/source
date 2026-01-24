/*
 * MMAP - Memory Map Management
 *
 * This module manages the physical memory map and working set lists (WSL).
 * Each physical page has an entry in the mmape_t array, and each process
 * has a working set list tracking which pages it has in memory.
 *
 * Key data structures:
 * - mmape_t: Per-physical-page entry (16 bytes each)
 * - ws_hdr_t: Working set list header (36 bytes each)
 *
 * Key addresses (m68k):
 * - MMAP global data: 0xE23284
 * - MMAP_$WSL (ws_hdr_t array): 0xE232B0
 * - MMAP_$WSL_HI_MARK (pid-to-wsl map): 0xE23CA6
 * - mmape_t array: 0xEB2800
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef MMAP_H
#define MMAP_H

#include "base/base.h"
#include "ml/ml.h"

/* MMAP status codes (module 0x06) */
#define status_$mmap_illegal_wsl_index          0x00060009
#define status_$mmap_illegal_pid                0x0006000a
#define status_$mmap_contig_pages_unavailable   0x0006000e

/* Forward declarations */
struct mmape_t;
struct ws_hdr_t;

/*
 * Memory Map Page Entry (mmape_t)
 *
 * One entry exists for each physical page. The base address is 0xEB2800
 * and entries are accessed as: mmape_base[vpn] (where each entry is 16 bytes).
 *
 * Pages are linked together in doubly-linked lists per working set list.
 */
typedef struct mmape_t {
    uint8_t     wire_count;         /* 0x00: Wire count (prevents paging when > 0) */
    uint8_t     seg_offset;         /* 0x01: Segment offset / page offset in segment */
    uint16_t    segment;            /* 0x02: Segment index (<<7 for segment base) */
    uint8_t     wsl_index;          /* 0x04: Working set list index this page belongs to */
    uint8_t     flags1;             /* 0x05: Flags - see MMAPE_FLAG1_* below */
    uint16_t    prev_vpn;           /* 0x06: Previous page in WSL doubly-linked list */
    uint8_t     priority;           /* 0x08: Page priority for replacement */
    uint8_t     flags2;             /* 0x09: Flags - see MMAPE_FLAG2_* below */
    uint16_t    next_vpn;           /* 0x0A: Next page in WSL doubly-linked list */
    uint32_t    disk_addr;          /* 0x0C: Disk address (used by AST layer for paging) */
} mmape_t;

/* mmape_t flags1 bit definitions */
#define MMAPE_FLAG1_IN_WSL      0x80    /* Page is installed in a working set list */
#define MMAPE_FLAG1_IMPURE      0x40    /* Page is impure (writable/data) vs pure (code) */

/* mmape_t flags2 bit definitions */
#define MMAPE_FLAG2_ON_DISK     0x80    /* Page has backing store / needs paging */
#define MMAPE_FLAG2_MODIFIED    0x40    /* Page has been modified (dirty) */

/*
 * Working Set List Header (ws_hdr_t)
 *
 * Each process has a working set list that tracks which pages it has
 * resident in memory. WSL indices 0-4 are special (0 = free pool, 5 = wired).
 * User processes use indices 5-69.
 */
typedef struct ws_hdr_t {
    uint8_t     flags;              /* 0x00: Flags - bit 7 = in_use */
    uint8_t     reserved1;          /* 0x01: Reserved */
    uint16_t    owner;              /* 0x02: Owner field (process/subsystem) */
    uint32_t    page_count;         /* 0x04: Number of pages in this WSL */
    uint32_t    scan_pos;           /* 0x08: Scan position for page replacement */
    uint32_t    head_vpn;           /* 0x0C: Head of page list (VPN) */
    uint32_t    max_pages;          /* 0x10: Maximum pages allowed */
    uint32_t    field_14;           /* 0x14: Unknown field */
    uint32_t    pri_timestamp;      /* 0x18: Priority update timestamp */
    uint32_t    ws_timestamp;       /* 0x1C: Working set timestamp */
    uint32_t    reserved2[2];       /* 0x20: Padding to 36 bytes (0x24) */
} ws_hdr_t;

/* WSL flags bit definitions */
#define WSL_FLAG_IN_USE         0x80    /* Working set list is in use */

/* WSL special indices */
#define WSL_INDEX_FREE_POOL     0       /* Free page pool */
#define WSL_INDEX_WIRED         5       /* Wired/locked pages */
#define WSL_INDEX_MIN_USER      5       /* Minimum user WSL index */
#define WSL_INDEX_MAX           69      /* Maximum WSL index (0x45) */

/* Maximum PID for pid-to-wsl mapping */
#define MMAP_MAX_PID            64      /* 0x40 */

/* Page type codes for FUN_00e0c514 (add page to WSL) */
#define MMAP_PAGE_TYPE_FREE     0       /* Free/available page */
#define MMAP_PAGE_TYPE_PURE     1       /* Pure (code) page */
#define MMAP_PAGE_TYPE_IMPURE   2       /* Impure (data) page - not modified */
#define MMAP_PAGE_TYPE_DIRTY_NF 3       /* Dirty page, no flush needed */
#define MMAP_PAGE_TYPE_DIRTY_FL 4       /* Dirty page, needs flush */

/*
 * MMAP Global Data Structure
 *
 * This represents the global MMAP state starting at 0xE23284.
 * Note: Some fields are accessed via offsets, this struct may not be complete.
 */
typedef struct mmap_globals_t {
    uint32_t    reserved1[7];               /* 0x00-0x1B: Reserved/unknown */
    uint32_t    ws_overflow_cnt;            /* 0x1C: Working set overflow count */
    uint32_t    steal_cnt;                  /* 0x20: Page steal count */
    uint32_t    alloc_pages;                /* 0x24: Total allocated pages */
    uint32_t    alloc_cnt;                  /* 0x28: Allocation count */
    ws_hdr_t    wsl[70];                    /* 0x2C: Working set list headers */
    /* After WSL array (at offset 0xA14): */
    /* uint32_t pageable_pages_lower_limit; */
    /* Offset 0xA22: uint16_t pid_to_wsl[65]; (pid-to-wsl mapping) */
} mmap_globals_t;

/*
 * Architecture-independent macros for page entry access
 * These isolate m68k-specific memory layout
 */
#if defined(M68K)
    #define MMAPE_BASE          ((mmape_t*)0xEB2800)
    #define MMAP_GLOBALS        ((mmap_globals_t*)0xE23284)
    #define MMAP_WSL            ((ws_hdr_t*)0xE232B0)
    #define MMAP_WSL_HI_MARK    (*(uint16_t*)0xE23CA6)
    #define MMAP_PID_TO_WSL     ((uint16_t*)0xE23CA6)
    #define PTE_BASE            ((uint16_t*)0xED5000)
#else
    /* For non-m68k platforms, these will be provided by platform init */
    extern mmape_t*         mmap_mmape_base;
    extern mmap_globals_t*  mmap_globals;
    extern ws_hdr_t*        mmap_wsl;
    extern uint16_t         mmap_wsl_hi_mark;
    extern uint16_t*        mmap_pid_to_wsl;
    extern uint16_t*        mmap_pte_base;

    #define MMAPE_BASE          mmap_mmape_base
    #define MMAP_GLOBALS        mmap_globals
    #define MMAP_WSL            mmap_wsl
    #define MMAP_WSL_HI_MARK    mmap_wsl_hi_mark
    #define MMAP_PID_TO_WSL     mmap_pid_to_wsl
    #define PTE_BASE            mmap_pte_base
#endif

/* Get mmape entry for a virtual page number */
#define MMAPE_FOR_VPN(vpn)      (&MMAPE_BASE[(vpn)])

/* Get WSL header for a WSL index */
#define WSL_FOR_INDEX(idx)      (&MMAP_WSL[(idx)])

/* Get WSL index for a process ID */
#define WSL_FOR_PID(pid)        (MMAP_PID_TO_WSL[(pid)])

/*
 * MMAP global data
 */
extern uint32_t MMAP_$PAGEABLE_PAGES_LOWER_LIMIT;
extern uint32_t MMAP_$WS_OVERFLOW;
extern uint32_t MMAP_$WS_REMOVE;
extern uint32_t MMAP_$WS_SCAN_CNT;
extern uint32_t MMAP_$ALLOC_CNT;
extern uint32_t MMAP_$ALLOC_PAGES;
extern uint32_t MMAP_$STEAL_CNT;
extern uint32_t MMAP_$REAL_PAGES;
extern uint32_t MMAP_$LPPN;             /* Lowest pageable page number */
extern uint32_t MMAP_$HPPN;             /* Highest pageable page number */

/*
 * Note: Internal error status arrays and other internal globals
 * are declared in mmap_internal.h. Include that header in .c files
 * that need access to internal MMAP data.
 */

/*
 * Function prototypes - Internal helpers
 */

/* Add a page to a working set list */
void mmap_$add_to_wsl(mmape_t *page, uint32_t vpn, uint16_t wsl_index, int8_t insert_at_head);

/* Add multiple pages to a working set list */
void mmap_$add_pages_to_wsl(uint32_t *vpn_array, uint16_t count, uint16_t wsl_index);

/* Remove a page from its current working set list */
void mmap_$remove_from_wsl(mmape_t *page, uint32_t vpn);

/* Trim pages from a working set list */
void mmap_$trim_wsl(uint16_t wsl_index, uint32_t pages_to_trim);

/* Move pages to a different WSL list type */
void mmap_$move_pages_to_wsl_type(uint32_t vpn_head, uint16_t page_type);

/*
 * Function prototypes - Public API
 */

/* Set working set priority timestamp */
void MMAP_$SET_WS_PRI(void);

/* Get working set size info */
void MMAP_$GET_WS_SIZ(uint16_t wsl_index, uint32_t *page_count,
                       uint32_t *field_40, uint32_t *max_pages, status_$t *status);

/* Get WSL index for a process ID */
void MMAP_$GET_WS_INDEX(uint16_t pid, uint16_t *wsl_index, status_$t *status);

/* Set maximum pages for a WSL */
void MMAP_$SET_WS_MAX(uint16_t wsl_index, uint32_t max_pages, status_$t *status);

/* Free a single page */
void MMAP_$FREE(uint32_t vpn);

/* Free a list of pages */
void MMAP_$FREE_LIST(uint32_t vpn_head);

/* Free and remove a page */
void MMAP_$FREE_REMOVE(mmape_t *page, uint32_t vpn);

/* Transfer impure page to different list */
void MMAP_$IMPURE_TRANSFER(mmape_t *page, uint32_t vpn);

/* Remove unavailable page */
void MMAP_$UNAVAIL_REMOV(uint32_t vpn);

/* Make a page available */
void MMAP_$AVAIL(uint32_t vpn);

/* Wire a page (prevent paging) */
void MMAP_$WIRE(uint32_t vpn);

/* Unwire a page (allow paging) */
void MMAP_$UNWIRE(uint32_t vpn);

/* Install a list of pages into a WSL */
void MMAP_$INSTALL_LIST(uint32_t *vpn_array, uint16_t count, int8_t use_wired);

/* Install pages for a specific process */
void MMAP_$INSTALL_PAGES(uint32_t *vpn_array, uint16_t count, uint16_t pid);

/* Free pages from an array */
void MMAP_$FREE_PAGES(uint32_t *vpn_array, uint16_t count);

/* Release pages for a process */
void MMAP_$RELEASE_PAGES(uint16_t pid, uint32_t *vpn_array, uint16_t count);

/* Purge all pages from a WSL */
void MMAP_$PURGE(uint16_t wsl_index);

/* Free a process's WSL */
void MMAP_$FREE_WSL(uint16_t pid);

/* Set WSL index for a process */
void MMAP_$SET_WS_INDEX(uint16_t pid, uint16_t *wsl_index);

/* Scan working set for page replacement */
uint32_t MMAP_$WS_SCAN(uint16_t wsl_index, int16_t mode, uint32_t pages_needed, uint32_t param4);

/* Get impure pages from a WSL */
void MMAP_$GET_IMPURE(uint16_t wsl_index, uint32_t *vpn_array, int8_t all_pages,
                       uint16_t max_pages, uint32_t *scanned, uint16_t *returned);

/* Allocate pages from a specific WSL */
void mmap_$alloc_pages_from_wsl(ws_hdr_t *wsl, uint32_t *vpn_array, uint16_t count);

/* Allocate pure (code) pages */
uint16_t MMAP_$ALLOC_PURE(uint32_t *vpn_array, uint16_t count);

/* Allocate free pages */
uint16_t MMAP_$ALLOC_FREE(uint32_t *vpn_array, uint16_t count);

/* Allocate contiguous pages (stub - always fails) */
void MMAP_$ALLOC_CONTIG(uint16_t count, uint32_t *pages_alloced, status_$t *status);

/* Reclaim pages into a WSL */
void MMAP_$RECLAIM(uint32_t *vpn_array, uint16_t count, int8_t use_wired);

/* Initialize MMAP subsystem */
void MMAP_$INIT(void *param);

/* Get remote pool (stub - returns input) */
uint32_t MMAP_$REMOTE_POOL(uint32_t param);

#endif /* MMAP_H */
