/*
 * AST Internal Header
 *
 * This header contains internal function prototypes and data structures
 * used only within the AST subsystem. External code should use ast.h.
 */

#ifndef AST_INTERNAL_H
#define AST_INTERNAL_H

#include "ast/ast.h"
#include "dxm/dxm.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "uid/uid.h"
#include "bat/bat.h"
#include "disk/disk.h"
#include "file/file.h"
#include "fm/fm.h"
#include "misc/misc.h"
#include "vtoc/vtoc.h"
#include "netlog/netlog.h"
#include "pmap/pmap.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "rem_file/rem_file.h"
#include "dbuf/dbuf.h"
#include "wp/wp.h"
#include "anon/anon.h"
#include "area/area.h"

/* MMAP working-set-list page counts at 0xE232xx (see mmap/mmap.h) */
extern uint32_t MMAP_$WSL_FREE_CNT;    /* 0xE232B4: MMAP_$WSL[0].page_count */
extern uint32_t MMAP_$WSL_PURE_CNT;    /* 0xE232D8: MMAP_$WSL[1].page_count */
extern uint32_t MMAP_$WSL_IMPURE_CNT;  /* 0xE232FC: MMAP_$WSL[2].page_count */

/* Internal AST functions (VTOC_$SEARCH_VOLUMES comes from vtoc/vtoc.h) */
extern void AST_$LOOKUP_WITH_HINTS(void *uid_info, uint32_t *vol_ptr, void *attrs, status_$t *status);
extern void AST_$DEACTIVATE_SEGMENT(aste_t *aste, uint32_t flags, status_$t *status);

/*
 * AST_$SET_ATTR_DISPATCH (0x00E04B00) is a Pascal procedure nested inside
 * ast_$set_attribute_internal: it declares no parameters and reads the
 * parent's frame through the static link in D4.  It is therefore emitted as a
 * static procedure in ast/set_attribute_internal.c and is deliberately not
 * declared here - it has no callable global ABI.
 */

/*
 * Internal helper functions
 */

/* Look up AOTE by UID - returns AOTE pointer */
aote_t *ast_$lookup_aote_by_uid(uid_t *uid);

/* Force lookup/activate AOTE for an object - returns AOTE pointer.
 *
 * `location` (A6+0x0C) is a LONGWORD and the callee reads it nine times;
 * the earlier note here claiming A6+0x0C was dead was wrong, and closing
 * bead source-sy5u meant reading the disassembly rather than the decompiler:
 *
 *   0x00E02194  move.l (0xc,A6),(0x8,A3)      aote->vol_uid := location
 *   0x00E021B2  tst.w  (0xc,A6) / smi         bit 31 -> aote+0xB9 remote flag
 *   0x00E021CC  tst.w  (0xc,A6) / bmi         local vs remote branch
 *   0x00E021D8  and.l  #0x7fffffff            low byte -> aote+0xB8 vol index
 *   0x00E021E6  and.l  #0xfffff               node id -> aote+0xB0
 *   0x00E021FA  move.l (0xc,A6),-(SP)         NETWORK_$GET_NET(location, ...)
 *   0x00E02228  and.l  #0x7fffffff / bne      zero selects the hint search
 *   0x00E02244  pea    (0xc,A6)               &location to AST_$LOOKUP_WITH_HINTS,
 *                                             which writes the resolved
 *                                             network/node back through it
 *   0x00E02260  move.l (0xc,A6),(0x8,A3)      re-store after that resolution
 *
 * The encoding is documented on aote_t.vol_uid in ast/ast.h -- it is the
 * same word AST_$GET_LOCATION returns (0x00E04766).  Of the thirteen
 * callers, AST_$MSTE_ACTIVATE_AND_WIRE passes mste->location (0x00E02F64)
 * and AST_$GET_DTV forwards its own second argument (0x00E054C6); the other
 * eleven pass a longword zero, meaning "location unknown, go find it".
 * AST_$GET_ATTRIBUTES is one of those eleven: A6-0x14 is a dedicated frame
 * cell cleared by `clr.l (-0x14,A6)` at 0x00E04822, never stored to again,
 * and pushed at 0x00E04832 -- a plain zero, not a volume or a segment.
 */
aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t location, status_$t *status, int8_t force);

/* Look up existing ASTE for AOTE/segment */
aste_t* ast_$lookup_aste(aote_t *aote, int16_t segment);

/* Look up or create ASTE for AOTE/segment */
aste_t* ast_$lookup_or_create_aste(aote_t *aote, uint16_t segment, status_$t *status);

/* Wait for page transition to complete */
void ast_$wait_for_page_transition(void);

/* Allocate pages - returns count, takes count_flags and ppn_array */
/*
 * ast_$allocate_pages - allocate `count` physical pages
 *
 * Three Pascal parameters, not two: `move.w (0x8,A6),D2w` at 0x00E00D4E
 * takes the requested count, `cmp.w (0xa,A6),D0w` at 0x00E00E56 compares
 * the running total against a SECOND word (the minimum that must be
 * obtained before the routine stops waking the purifier), and
 * `movea.l (0xc,A6),A4` at 0x00E00D52 takes the array.  Every caller in
 * the image pushes (count, 1, ppn_array).
 */
int16_t ast_$allocate_pages(int16_t count, int16_t min_count,
                            uint32_t *ppn_array);

/* Clear transition bits in segment map */
void ast_$clear_transition_bits(uint32_t *segmap, uint16_t count);

/* Setup page read */
void ast_$setup_page_read(aste_t *aste, uint32_t *segmap, uint16_t start_page,
                          uint16_t count, uint16_t flags, status_$t *status);

/*
 * Allocate and zero the pages for a copy-on-write run.
 *
 * A nested procedure of AST_$TOUCH.  Its two real stack arguments are a
 * pointer into the segment map (0x08,A6) and a page count (0x0C,A6); the
 * remaining three are AST_$TOUCH's own `flags` (only bit 1 is read, via
 * `btst.b #0x1,(0x1d,A2)`), `ppn_array` (0x14,A2) and `status` (0x18,A2),
 * reached through the static link and passed explicitly here.
 */
int16_t ast_$count_valid_pages(uint32_t *segmap_entry, int16_t count,
                               uint16_t touch_flags,
                               uint32_t *ppn_array,
                               status_$t *status);

/* Read area pages from disk */
int16_t ast_$read_area_pages(aste_t *aste, uint32_t *segmap, uint32_t *ppn_array,
                             uint16_t start_page, uint16_t count,
                             status_$t *status);

/* Read area pages from network */
int16_t ast_$read_area_pages_network(aste_t *aste, uint32_t *segmap, uint32_t *ppn_array,
                                     uint16_t start_page, uint16_t count, uint8_t flags,
                                     status_$t *status);

/* Process AOTE flags/flush - returns completion flags */
uint16_t ast_$process_aote(aote_t *aote, uint8_t flags1, uint16_t flags2,
                           uint16_t flags3, status_$t *status);

/* Free/release AOTE */
void ast_$release_aote(aote_t *aote);

/* Allocate new AOTE */
aote_t* ast_$allocate_aote(void);

/* Purify/flush AOTE */
void ast_$purify_aote(aote_t *aote, uint16_t flags, status_$t *status);

/* Update ASTE/segment map */
void ast_$update_aste(aste_t *aste, segmap_entry_t *segmap, uint16_t flags,
                      status_$t *status);

/* Invalidate pages with wait */
status_$t ast_$invalidate_with_wait(uint16_t end_page);

/* Invalidate pages without wait */
void ast_$invalidate_no_wait(uint16_t end_page);

/* Flush installed pages */
void ast_$flush_installed_pages(void);

/*
 * Set attribute on object (0x00E05214).
 *
 * The fifth argument is the caller's subject record: AST_$SET_ATTR_DISPATCH
 * reads it uplevel at (0x14,A6) for attribute type 0x14.  Callers that use
 * neither 0x14 nor the ACL merge pass NULL, as the recursive ADD_REFCOUNT call
 * at 0xE051A4 does.
 */
void ast_$set_attribute_internal(uid_t *uid, uint16_t attr_type, void *value,
                                 boolean wait_flag, ast_$subject_t *subject,
                                 clock_t *clock_info, status_$t *status);

/* Validate UID and return status */
status_$t ast_$validate_uid(uid_t *uid, uint32_t flags);

/*
 * Internal global variables
 */

/* Volume info count at 0xE1E0A0 (offset 0x420 from globals) */
extern uint16_t ast_$vol_info_count;
#define DAT_00e1e0a0 ast_$vol_info_count

/*
 * Dismount eventcount at 0xE1E088 (offset 0x408).  AST_$DISMOUNT waits on it
 * (0xE06A36) and reads its value field for the wait target (0xE06A12), so it is
 * a 12-byte ec_$eventcount_t, not a bare longword.  See AST_$DISM_EC in ast.h.
 */

/* Volume index array at 0xE1E092 */
extern int16_t ast_$vol_indices[];
#define DAT_00e1e092 ast_$vol_indices

/* Clobbered UID storage at 0xE1E110 (offset 0x490) */
extern uid_t ast_$clobbered_uid;
#define DAT_00e1e110 ast_$clobbered_uid

/* Dismount failed AOTE pointer */
extern aote_t* AST_$DISMOUNT_FAILED_PTR;

/* Attribute timestamp mask at A5+0x48C (0xE1E10C), used by AST_$SET_ATTR_DISPATCH */
#if defined(ARCH_M68K)
#define AST_$ATTR_TIMESTAMP_MASK (*(uint32_t *)((char *)__A5_BASE() + 0x48C))
#else
extern uint32_t ast_$attr_timestamp_mask;
#define AST_$ATTR_TIMESTAMP_MASK ast_$attr_timestamp_mask
#endif

/*
 * Cell holding AST_$SET_TROUBLE's address.  AST_$SAVE_CLOBBERED_UID pushes
 * the ADDRESS of this cell to DXM_$ADD_CALLBACK (0x00E0724E);
 * dxm_$callback_t keeps the queue entry 16 bytes on every target
 * (source-wy9y).
 *
 * The cell holds 0x00e071ea (AST_$SET_TROUBLE); defined in ast/ast_data.c.
 */
extern dxm_$callback_t PTR_AST_$SET_TROUBLE_00e07272;

/* Zero buffer for page operations (1KB = 256 uint32_t) */
extern uint32_t AST_$ZERO_BUFF[256];

/* Duplicate AOTE error status */
extern status_$t status_$_00e2f1d0;

/*
 * AOTE management globals
 */

/* AOTE array bounds and scanning */
extern aote_t *aote_array_start;        /* Start of AOTE array */
extern aote_t *ast_$aote_end;           /* End of AOTE array */
extern aote_t *ast_$aote_scan_pos;      /* Current scan position for allocation */
extern aote_t *ast_$free_aote_head;     /* Head of free AOTE list */
extern uint16_t ast_$free_aotes;        /* Count of free AOTEs */
extern uint16_t ast_$size_aot;          /* Size of AOTE array */

/* AOTE hash table (for UID lookup) */
extern aote_t **ast_aoth_base;          /* Base of AOTE hash table */
extern void *ast_hash_table_info;       /* Hash table parameters */
extern uint32_t ast_$aote_seqn;         /* AOTE sequence number (for race detection) */

/* AOTE allocation statistics */
extern uint32_t ast_$alloc_total_aot;   /* Total allocation attempts */
extern uint32_t ast_$alloc_worst_aot;   /* Worst-case allocation count */
extern uint32_t ast_$alloc_fail_cnt;    /* Allocation failure count */
extern uint32_t ast_$alloc_try_cnt;     /* Current try count */

/* Failed UID tracking (for error reporting) */
extern uint32_t ast_$failed_uid_high;   /* High word of failed UID */
extern uint32_t ast_$failed_uid_low;    /* Low word of failed UID */
extern uint32_t ast_$failed_flags;      /* Flags for failed operation */

/* Network info flags pointer */
extern void *net_info_flags;

/* Volume reference tracking */
extern int16_t vol_ref_counts[];        /* Per-volume reference counts */
extern uint16_t vol_dismount_mask;      /* Bitmask of dismounting volumes */
extern ec_$eventcount_t vol_dismount_ec; /* Dismount completion eventcount */

/* ASTE allocation functions */
extern aste_t *AST_$ALLOCATE_ASTE(void);
extern void AST_$FREE_ASTE(aste_t *aste);
extern void AST_$WAIT_FOR_AST_INTRANS(void);

/* Process info for statistics - include proc1.h for PROC1_$CURRENT, etc. */
#include "proc1/proc1.h"

/* Per-process page/network stats */
extern int32_t proc_page_stats[];
extern int32_t proc_net_stats[];

#endif /* AST_INTERNAL_H */
