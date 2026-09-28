/*
 * PMAP - Page Map Management
 *
 * This module provides page map management for Domain/OS.
 * It handles flushing dirty pages to disk, managing page purifier
 * processes, and working set scanning.
 *
 * Key concepts:
 * - Page flushing: Writing modified pages back to disk
 * - Purifier: Background process that cleans dirty pages
 * - Working set: Pages actively used by a process
 *
 * The PMAP layer works closely with MMAP (Memory Map) and AST
 * (Active Segment Table) to manage the page cache.
 *
 * Memory layout (m68k):
 * - PMAP globals base: 0xE24D44 + offsets
 * - Hardware page table: 0xFFB802
 * - PMAPE base: 0xEB2800
 * - Segment map base: 0xED5000
 */

#ifndef PMAP_H
#define PMAP_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "mmap/mmap.h"   /* MMAP_$WSL_*_CNT, MMAP_WSL */

/*
 * Forward declarations
 */
struct aste_t;
struct aote_t;

/*
 * PMAP Global Variables
 */

extern ec_$eventcount_t  PMAP_$PAGES_EC;
extern ec_$eventcount_t  PMAP_$L_PURIFIER_EC;
extern ec_$eventcount_t  PMAP_$R_PURIFIER_EC;
extern uint16_t         PMAP_$LOW_THRESH;
extern uint16_t         PMAP_$MID_THRESH;
extern uint16_t         PMAP_$WS_INTERVAL;
extern uint32_t         PMAP_$T_PUR_SCANS;
/* Working-set tuning / statistics read and written by OSINFO_$GET_MMAP */
extern uint16_t         PMAP_$MAX_WS_INTERVAL;  /* 0xE254D6 */
extern uint16_t         PMAP_$MIN_WS_INTERVAL;  /* 0xE254D4 */
extern uint32_t         PMAP_$IDLE_INTERVAL;    /* 0xE25484 */
extern uint32_t         PMAP_$PUR_L_CNT;        /* 0xE25490 */
extern uint32_t         PMAP_$PUR_R_CNT;        /* 0xE2548C */
extern uint16_t         PMAP_$SCAN_FRACT;       /* 0xE254CC */
extern int8_t           PMAP_$SHUTTING_DOWN_FLAG;
extern uint16_t         PMAP_$CURRENT_SLOT;

/*
 * External references to other modules
 */
/* MMAP_$WSL_*_CNT are macros over MMAP_WSL[].page_count; see mmap/mmap.h. */

/*
 * Lock IDs
 */
#define PMAP_LOCK_ID                    0x14    /* PMAP lock */
#define PROC_LOCK_ID                    0x0D    /* Process lock */

/* LOG_$UPDATE, LOG_$LOGFILE_PTR - declared in log/log.h */

/*
 * NETLOG functions - declared in ast/ast.h
 * Note: signature is NETLOG_$LOG_IT(type, uid, seg, page, ppn, count, param7, param8)
 */

/*
 * Internal helper functions.
 *
 * pmap_$write_page, pmap_$update_seg_map and pmap_$flush_write_batch are
 * PMAP-private (the latter two are nested procedures of PMAP_$FLUSH);
 * their prototypes live in pmap/pmap_internal.h.
 */

/*
 * Error strings
 */
extern status_$t status_$t_00e13a14;
extern status_$t status_$t_00e145ec;

/*
 * Function prototypes - Page flushing
 */
int16_t PMAP_$FLUSH(struct aste_t *aste, uint32_t *segmap, uint16_t start_page,
                    int16_t count, uint16_t flags, status_$t *status);

/*
 * Function prototypes - Purifier control
 */
void PMAP_$WAKE_PURIFIER(int8_t wait);

/*
 * Function prototypes - Purifier processes (run as background tasks)
 */
void PMAP_$PURIFIER_L(void);
void PMAP_$PURIFIER_R(void);

/*
 * Function prototypes - Callbacks
 */
void PMAP_$UPDATE_CALLBACK(void);
void PMAP_$T_PURIF_CALLBACK(void);
/* The time queue hands its callbacks the address of a longword holding the
 * element address (time_$callback_arg_t); see pmap/ws_scan_callback.c. */
void PMAP_$WS_SCAN_CALLBACK(void *arg);

/*
 * Function prototypes - Working set management
 */
void PMAP_$INIT_WS_SCAN(uint16_t index, int16_t param);
void PMAP_$PURGE_WS(int16_t index, int16_t flags);

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
/*
 * Bit 5 of the flags byte (bit 13 of the entry's first word): the page is
 * installed in the MMU.  mmap_$trim_wsl tests it with `btst.l #0xd' on the
 * word (0x00E0C858) and clears it with `bclr.b #5' before MMU_$REMOVE
 * (0x00E0C85E-0x00E0C866).
 */
#define PMAP_SEGMAP_INSTALLED   0x20
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
 * The segment-map entry as the LONGWORD the PMAP code tests it as
 * (pmap_segmap_entry_t above is its first byte).  PMAP_$FLUSH reads the
 * high word with `tst.w (A2)` / `btst.l #0xe` / `btst.l #0xd`, the low word
 * (the VPN) with `move.w (0x2,A2)`, and sets/clears bits 7 and 5 of the
 * first byte (`bset.b #7,(A2)`, `bclr.b #5,(A2)`).  Appended 2026-09-27.
 */
#define PMAP_SEGMAP_L_WRITING    0x80000000u  /* first byte bit 7 = PMAP_SEGMAP_WRITING */
#define PMAP_SEGMAP_L_VALID      0x40000000u  /* high word bit 14: entry holds a VPN */
#define PMAP_SEGMAP_L_INSTALLED  0x20000000u  /* first byte bit 5 = PMAP_SEGMAP_INSTALLED */
#define PMAP_SEGMAP_L_VPN_MASK   0x0000FFFFu  /* low word: the VPN */

#endif /* PMAP_H */
