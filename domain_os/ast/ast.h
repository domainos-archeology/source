/*
 * AST - Active Segment Table Management
 *
 * This module provides active segment management for Domain/OS.
 * It sits above the MMU and MMAP layers, managing the relationship
 * between objects (files), segments, and physical pages.
 *
 * Key concepts:
 * - AOTE (Active Object Table Entry): Represents a cached object (file)
 * - ASTE (Active Segment Table Entry): Represents a segment mapping
 * - Segment Map: 32 page entries per segment (1KB pages, 32KB segments)
 *
 * Key data structures:
 * - AOTE: 192 bytes (0xC0) per entry, hash-chained by UID
 * - ASTE: 20 bytes (0x14) per entry
 * - Segment Map: 128 bytes (0x80) per segment at 0xED5000
 *
 * Memory layout (m68k):
 * - AST globals: 0xE1DC80
 * - ASTE array: 0xEC5400
 * - AOTE area: grows from 0xEC7B60
 * - Segment maps: 0xED5000
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef AST_H
#define AST_H

#include "base/base.h"
#include "ml/ml.h"
#include "ec/ec.h"

/* AST status codes (module 0x03) */
#define status_$ast_incompatible_request 0x00030006
#define status_$ast_write_concurrency_violation 0x00030005
#define status_$ast_eof 0x00030001

/* PMAP status codes (module 0x05) */
#define status_$pmap_bad_assoc 0x00050006
#define status_$pmap_page_null 0x00050008
#define status_$pmap_read_concurrency_violation 0x0005000A

/* OS status codes (module 0x03) */
#define status_$os_only_local_access_allowed 0x0003000A

/* File status codes (module 0x0F) */
#define file_$object_not_found 0x000F0001

/*
 * Forward declarations
 */
struct aote_t;
struct aste_t;

/*
 * ASTE (Active Segment Table Entry)
 *
 * Represents a segment mapping. Each ASTE links an object (via AOTE)
 * to a segment number. Size: 20 bytes (0x14).
 */
typedef struct aste_t {
  struct aste_t *next; /* 0x00: Next ASTE in chain (or free list) */
  struct aote_t *aote; /* 0x04: Pointer to owning AOTE */
  uint16_t segment;    /* 0x08: First segment number << 5 (or page offset) */
  uint16_t unknown_0a; /* 0x0A: Unknown */
  uint16_t timestamp;  /* 0x0C: Timestamp for LRU */
  uint16_t seg_index;  /* 0x0E: Segment index (for segment map lookup) */
  uint8_t page_count;  /* 0x10: Number of pages mapped */
  uint8_t wire_count;  /* 0x11: Wire/reference count */
  uint16_t flags;      /* 0x12: Flags - see ASTE_FLAG_* below */
} aste_t;

/* ASTE flags (at offset 0x12) */
#define ASTE_FLAG_IN_TRANS 0x8000 /* In transition (being modified) */
#define ASTE_FLAG_LOCKED 0x4000   /* Locked - cannot be freed */
#define ASTE_FLAG_DIRTY 0x2000    /* Dirty - needs writeback */
#define ASTE_FLAG_AREA 0x1000     /* Area mapping (vs single segment) */
#define ASTE_FLAG_REMOTE 0x0800   /* Remote (network) object */
#define ASTE_FLAG_BUSY 0x0040     /* Busy flag (cleared on scan) */

/* ASTE index mask */
#define ASTE_INDEX_MASK 0x01FF /* 9-bit index */

/*
 * AOTE (Active Object Table Entry)
 *
 * Represents a cached object (file). Contains object attributes,
 * UID, and links to ASTEs. Size: 192 bytes (0xC0).
 */
typedef struct aote_t {
  struct aote_t *hash_next; /* 0x00: Next in hash chain */
  struct aste_t *aste_list; /* 0x04: List of ASTEs for this object */
  uint32_t vol_uid;         /* 0x08: Volume UID or network info */
  /* 0x0C - 0x9B: Object attributes (144 bytes = 0x90) */
  uint8_t attributes[144];
  /* 0x9C - 0xBB: Object UID and related info (32 bytes) */
  uid_t obj_uid;          /* 0x9C: Object UID (8 bytes) */
  uint32_t unknown_a4[6]; /* 0xA4: Additional UID/info */
  /* 0xBC - 0xBF: Flags and status */
  uint16_t status_flags; /* 0xBC: Status flags */
  uint8_t ref_count;     /* 0xBE: Reference count */
  uint8_t flags;         /* 0xBF: Flags - see AOTE_FLAG_* below */
} aote_t;

/* AOTE flags (at offset 0xBF) */
#define AOTE_FLAG_IN_TRANS 0x80 /* In transition */
#define AOTE_FLAG_BUSY 0x40     /* Busy/locked */
#define AOTE_FLAG_DIRTY 0x20    /* Dirty - needs writeback */
#define AOTE_FLAG_TOUCHED 0x10  /* Recently accessed */

/* AOTE remote flag (at offset 0xB9) */
#define AOTE_REMOTE_FLAG 0x80 /* Object is remote (network) */

/*
 * Segment Map Entry
 *
 * Each segment has 32 page entries (4 bytes each = 128 bytes total).
 * Located at 0xED5000 + (segment_index * 0x80).
 */
typedef struct segmap_entry_t {
  uint32_t entry; /* Page mapping entry */
} segmap_entry_t;

/* Segment map entry flags (high byte) */
#define SEGMAP_IN_TRANS 0x80000000      /* Page in transition */
#define SEGMAP_VALID 0x40000000         /* Valid mapping */
#define SEGMAP_WIRED 0x20000000         /* Page is wired */
#define SEGMAP_COPY_ON_WRITE 0x00400000 /* Copy-on-write page */
#define SEGMAP_PPN_MASK 0x007FFFFF      /* Physical page number mask */

/*
 * AST Global State
 *
 * Global variables for the AST subsystem, based at 0xE1DC80.
 */


/*
 * Architecture-independent macros for AST access
 */
#if defined(M68K)
/* AST globals base */
#define AST_GLOBALS_BASE 0xE1DC80

/* AOTE hash table (at base + 0x00, 256 entries * 4 bytes) */
#define AOTH (*(uint32_t *)0xE1DC80)

/* ASTE array base */
#define ASTE_BASE ((aste_t *)0xEC5400)

/* Segment map base */
#define SEGMAP_BASE ((segmap_entry_t *)0xED5000)

/* AST globals at various offsets from base */
#define AST_$AOTE_LIMIT (*(aote_t **)0xE1E074)        /* 0x3F4 */
#define AST_$FREE_ASTE_HEAD (*(aste_t **)0xE1E078)    /* 0x3F8 */
#define AST_$ASTE_SCAN_POS (*(aste_t **)0xE1E07C)     /* 0x3FC */
#define AST_$ASTE_LIMIT (*(aste_t **)0xE1E080)        /* 0x400 */
#define AST_$DISM_SEQN (*(uint32_t *)0xE1E084)        /* 0x404 */
#define AST_$UPDATE_SCAN (*(aote_t **)0xE1E104)       /* 0x484 */
#define AST_$UPDATE_TIMESTAMP (*(uint16_t *)0xE1E108) /* 0x488 */
#define AST_$AOTE_SEQN (*(uint32_t *)0xE1E0B4)        /* 0x434 */

/* Event counters */
#define AST_$AST_IN_TRANS_EC (*(ec_$eventcount_t *)0xE1E0A8)  /* 0x428 */
#define AST_$PMAP_IN_TRANS_EC (*(ec_$eventcount_t *)0xE1E0CC) /* 0x44C */

/* Statistics */
#define AST_$ALLOC_WORST_AST (*(uint32_t *)0xE1E0C4) /* 0x444 */
#define AST_$ALLOC_TOTAL_AST (*(uint32_t *)0xE1E0C8) /* 0x448 */
#define AST_$WS_FLT_CNT (*(uint32_t *)0xE1E0D8)      /* 0x458 */
#define AST_$PAGE_FLT_CNT (*(uint32_t *)0xE1E0DC)    /* 0x45C */
#define AST_$FREE_ASTES (*(uint16_t *)0xE1E0E8)      /* 0x468 */
#define AST_$GROW_AHEAD_CNT (*(uint16_t *)0xE1E0EC)  /* 0x46C */
#define AST_$SIZE_AOT (*(uint16_t *)0xE1E0EE)        /* 0x46E */
#define AST_$SIZE_AST (*(uint16_t *)0xE1E0F0)        /* 0x470 */
#define AST_$ASTE_AREA_CNT (*(uint16_t *)0xE1E0F2)   /* 0x472 */
#define AST_$ASTE_R_CNT (*(uint16_t *)0xE1E0F4)      /* 0x474 */
#define AST_$ASTE_L_CNT (*(uint16_t *)0xE1E0F6)      /* 0x476 */
#else
/* For non-m68k platforms */
extern uint8_t *ast_globals_base;
extern uint32_t ast_aoth[];
extern aste_t *ast_aste_base;
extern segmap_entry_t *ast_segmap_base;

#define AST_GLOBALS_BASE ((uintptr_t)ast_globals_base)
extern aote_t *ast_aote_limit;
extern aste_t *ast_free_aste_head;
extern aste_t *ast_aste_scan_pos;
extern aste_t *ast_aste_limit;
extern uint32_t ast_dism_seqn;
extern aote_t *ast_update_scan;
extern uint16_t ast_update_timestamp;
extern uint32_t ast_aote_seqn;
extern ec_$eventcount_t ast_ast_in_trans_ec;
extern ec_$eventcount_t ast_pmap_in_trans_ec;
extern uint32_t ast_alloc_worst;
extern uint32_t ast_alloc_total;
extern uint32_t ast_ws_flt_cnt;
extern uint32_t ast_page_flt_cnt;
extern uint16_t ast_free_astes;
extern uint16_t ast_grow_ahead_cnt;
extern uint16_t ast_size_aot;
extern uint16_t ast_size_ast;
extern uint16_t ast_aste_area_cnt;
extern uint16_t ast_aste_r_cnt;
extern uint16_t ast_aste_l_cnt;

#define AOTH ast_aoth[0]
#define ASTE_BASE ast_aste_base
#define SEGMAP_BASE ast_segmap_base
#define AST_$AOTE_LIMIT ast_aote_limit
#define AST_$FREE_ASTE_HEAD ast_free_aste_head
#define AST_$ASTE_SCAN_POS ast_aste_scan_pos
#define AST_$ASTE_LIMIT ast_aste_limit
#define AST_$DISM_SEQN ast_dism_seqn
#define AST_$UPDATE_SCAN ast_update_scan
#define AST_$UPDATE_TIMESTAMP ast_update_timestamp
#define AST_$AOTE_SEQN ast_aote_seqn
#define AST_$AST_IN_TRANS_EC ast_ast_in_trans_ec
#define AST_$PMAP_IN_TRANS_EC ast_pmap_in_trans_ec
#define AST_$ALLOC_WORST_AST ast_alloc_worst
#define AST_$ALLOC_TOTAL_AST ast_alloc_total
#define AST_$WS_FLT_CNT ast_ws_flt_cnt
#define AST_$PAGE_FLT_CNT ast_page_flt_cnt
#define AST_$FREE_ASTES ast_free_astes
#define AST_$GROW_AHEAD_CNT ast_grow_ahead_cnt
#define AST_$SIZE_AOT ast_size_aot
#define AST_$SIZE_AST ast_size_ast
#define AST_$ASTE_AREA_CNT ast_aste_area_cnt
#define AST_$ASTE_R_CNT ast_aste_r_cnt
#define AST_$ASTE_L_CNT ast_aste_l_cnt
#endif

/* Get ASTE entry by index */
#define ASTE_FOR_INDEX(idx) (&ASTE_BASE[(idx)])

/* Get segment map for segment index */
#define SEGMAP_FOR_SEG(seg)                                                    \
  ((segmap_entry_t *)((char *)SEGMAP_BASE + ((seg) << 7)))

/* Maximum sizes */
#define AST_MAX_AOTE 0x118 /* 280 entries */
#define AST_MAX_ASTE 0x1F8 /* 504 entries */
#define AST_MIN_AOTE 0x28  /* 40 entries */
#define AST_MIN_ASTE 0x50  /* 80 entries */

/*
 * External references
 */
extern uint32_t NODE_$ME;
extern int8_t NETWORK_$REALLY_DISKLESS;
extern int8_t NETLOG_$OK_TO_LOG;

/*
 * Error status codes
 */
extern status_$t Some_ASTE_Error;
extern status_$t OS_PMAP_mismatch_err;
extern status_$t OS_MMAP_bad_install;

/*
 * System functions
 */
extern void WP_$CALLOC(uint32_t *ppn, status_$t *status);
extern void NETWORK_$INSTALL_NET(uint32_t node, void *info, status_$t *status);
extern void NETWORK_$AST_GET_INFO(void *uid_info, uint16_t *flags, void *attrs,
                                  status_$t *status);
extern void DBUF_$UPDATE_VOL(uint16_t flags, uid_t *uid);
extern void NETLOG_$LOG_IT(uint16_t type, void *uid, uint16_t seg,
                           uint16_t page, uint16_t ppn, uint16_t count,
                           uint16_t remote, uint16_t unused);

/*
 * Lock IDs used by AST
 */
#define AST_LOCK_ID 0x12  /* Main AST lock */
#define PMAP_LOCK_ID 0x14 /* PMAP lock */

/*
 * Segment map entry flags
 */
#define SEGMAP_FLAG_IN_TRANS 0x80000000  /* Page in transition */
#define SEGMAP_FLAG_IN_USE 0x40000000    /* Page is in use (installed) */
#define SEGMAP_FLAG_INSTALLED 0x20000000 /* Page installed in MMU */
#define SEGMAP_FLAG_COW 0x00400000       /* Copy-on-write */
#define SEGMAP_DISK_ADDR_MASK 0x007FFFFF /* Disk address / PPN mask */

/*
 * PMAPE (Physical Memory Attribute Page Entry)
 * Structure for tracking physical page attributes.
 * Located at PMAPE_BASE (0xEB2800) + ppn * 16
 */
typedef struct pmape_t {
  uint8_t ref_count;   /* 0x00: Reference count */
  uint8_t page_offset; /* 0x01: Page offset in segment */
  uint16_t seg_index;  /* 0x02: Segment index */
  uint32_t unknown_04; /* 0x04: Unknown */
  uint32_t unknown_08; /* 0x08: Unknown */
  uint32_t disk_addr;  /* 0x0C: Disk address / physical info */
} pmape_t;

/* PMAPE base addresses */
#if defined(M68K)
#define PMAPE_BASE 0xEB2800
#define MMAP_BASE 0xEB2800 /* Same as PMAPE_BASE */
#else
extern uint8_t *pmape_base;
extern uint8_t *mmap_base;
#define PMAPE_BASE pmape_base
#define MMAP_BASE mmap_base
#endif

/*
 * Function prototypes - Initialization
 */
void AST_$INIT(void);
void AST_$ACTIVATE_AOTE_CANNED(uint32_t *attrs, uint32_t *obj_info);

/*
 * Function prototypes - ASTE management
 */
aste_t *AST_$ALLOCATE_ASTE(void);
void AST_$FREE_ASTE(aste_t *aste);
uint16_t AST_$ADD_ASTES(uint16_t *count, status_$t *status);
uint16_t AST_$ADD_AOTES(uint16_t *count, status_$t *status);
/* Request structure for AST_$LOCATE_ASTE */
typedef struct locate_request_t {
  uint32_t uid_high; /* 0x00: UID high word */
  uint32_t uid_low;  /* 0x04: UID low word */
  uint16_t segment;  /* 0x08: Segment number */
  uint16_t hint;     /* 0x0A: ASTE index hint (low 9 bits) */
} locate_request_t;

aste_t *AST_$LOCATE_ASTE(locate_request_t *request);

/*
 * Function prototypes - Page operations
 */
void AST_$PAGE_ZERO(uint32_t ppn);
void AST_$INVALIDATE_PAGE(aste_t *aste, uint32_t *segmap_entry, uint32_t ppn);
void AST_$FREE_PAGES(aste_t *aste, int16_t start_page, int16_t end_page,
                     int16_t flags);
void AST_$RELEASE_PAGES(aste_t *aste, int8_t return_to_pool);
void AST_$FETCH_PMAP_PAGE(void *uid_info, uint32_t *output_buf, uint16_t flags,
                          status_$t *status);

/*
 * MSTE (Memory Segment Table Entry) structure
 * Contains segment mapping information
 */
typedef struct mste_t {
  uid_t uid;           /* 0x00: Object UID */
  uint16_t segment;    /* 0x08: Segment number */
  uint16_t unknown_0a; /* 0x0A: Unknown */
  uint32_t vol_uid;    /* 0x0C: Volume UID */
} mste_t;

/*
 * Function prototypes - Activation and wiring
 */
aste_t *AST_$MSTE_ACTIVATE_AND_WIRE(mste_t *mste, status_$t *status);
aste_t *AST_$ACTIVATE_AND_WIRE(uid_t *uid, uint16_t seg, status_$t *status);
uint16_t AST_$TOUCH(aste_t *aste, uint32_t mode, uint16_t page, uint16_t count,
                    uint32_t *ppn_array, status_$t *status, uint16_t flags);
void AST_$TOUCH_AREA(aste_t *aste, uint32_t mode, uint16_t start,
                     uint16_t count, status_$t *status, uint16_t flags);

/*
 * Function prototypes - Association
 */
void AST_$PMAP_ASSOC(aste_t *aste, uint16_t page, uint32_t ppn, uint16_t flags1,
                     uint16_t flags2, status_$t *status);
void AST_$ASSOC(uid_t *uid, uint16_t seg, uint32_t mode, uint16_t page,
                uint16_t flags, uint32_t ppn, status_$t *status);
void AST_$ASSOC_AREA(uint16_t seg_index, int16_t page, uint32_t ppn,
                     status_$t *status);

/*
 * Function prototypes - Copy operations
 */
void AST_$COPY_AREA(uint16_t partner_index, uint16_t unused, aste_t *src_aste,
                    aste_t *dst_aste, uint16_t start_seg, char *buffer,
                    status_$t *status);

/*
 * Function prototypes - Attributes
 */
void AST_$GET_LOCATION(uint32_t *uid_info, uint16_t flags, uint32_t unused,
                       uint32_t *vol_uid_out, status_$t *status);
void AST_$GET_ATTRIBUTES(uid_t *uid, uint16_t flags, void *attrs,
                         status_$t *status);
void AST_$GET_COMMON_ATTRIBUTES(uid_t *uid, uint16_t flags, void *attrs,
                                status_$t *status);
void AST_$GET_ACL_ATTRIBUTES(uid_t *uid, uint16_t flags, void *acl,
                             status_$t *status);
void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value,
                        status_$t *status);
void AST_$SET_ATTR(uid_t *uid, int16_t attr_id, uint32_t value, uint8_t flags,
                   uint32_t *clock, status_$t *status);
void AST_$GET_DTV(uid_t *uid, uint32_t unused, uint32_t *dtv,
                  status_$t *status);
uint8_t AST_$SET_DTS(uint16_t flags, uid_t *uid, uint32_t *dtv,
                     uint32_t *access_time, status_$t *status);

/*
 * Function prototypes - Object operations
 */
void AST_$LOAD_AOTE(uint32_t *attrs, uint32_t *obj_info);
uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused,
                     status_$t *status);
void AST_$COND_FLUSH(uid_t *uid, uint32_t *timestamp, status_$t *status);
void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   uint8_t *result, status_$t *status);
void AST_$INVALIDATE(uid_t *uid, uint32_t start_page, uint32_t count,
                     int16_t flags, status_$t *status);
void AST_$RESERVE(uid_t *uid, uint32_t start_byte, uint32_t byte_count,
                  status_$t *status);
void AST_$DISMOUNT(uint16_t vol_index, uint8_t flags, status_$t *status);
void AST_$GET_SEG_MAP(uint32_t *uid_info, uint32_t start_offset,
                      uint32_t unused, uid_t *vol_uid, uint32_t count,
                      uint16_t flags, uint32_t *output, status_$t *status);

/*
 * Function prototypes - Maintenance
 */
void AST_$UPDATE(void);
uint32_t AST_$GET_DISM_SEQN(void);
void AST_$SET_TROUBLE(uid_t **uid_ptr);
void AST_$SAVE_CLOBBERED_UID(uid_t *uid);
uint8_t AST_$REMOVE_CORRUPTED_PAGE(uint32_t ppn);

/*
 * Function prototypes - Synchronization
 */
void AST_$WAIT_FOR_AST_INTRANS(void);

#endif /* AST_H */
