/*
 * AST Internal Header
 *
 * This header contains internal function prototypes and data structures
 * used only within the AST subsystem. External code should use ast.h.
 */

#ifndef AST_INTERNAL_H
#define AST_INTERNAL_H

#include "ast/ast.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "uid/uid.h"
/* Note: pmap/pmap.h excluded due to NETLOG_$LOG_IT signature conflict */

/* Restore AST's PMAPE_BASE after mmap.h (mmap.h uses a different value) */
#ifdef PMAPE_BASE
#undef PMAPE_BASE
#endif
#define PMAPE_BASE 0xEB2800  /* M68K physical address */

/*
 * Cross-subsystem function declarations needed by AST
 * These should eventually be moved to their respective headers
 */

/* OS functions */
extern void CRASH_SYSTEM(const status_$t *status_p);

/* VTOC/VTOCE functions */
extern void VTOC_$LOOKUP(void *uid_info, status_$t *status);
extern void VTOCE_$READ(void *uid_info, void *attrs, status_$t *status);
extern void VTOCE_$WRITE(void *uid_info, void *attrs, uint8_t flags, status_$t *status);
extern void VTOCE_$LOOKUP_FM(void *uid_info, uint16_t segment, int16_t flags,
                              uint32_t *vtoce_ptr, int32_t *block_delta, status_$t *status);

/* FM (File Map) functions */
extern void FM_$READ(void *uid_info, uint32_t vtoce_ptr, uint16_t segment,
                     void *buffer, status_$t *status);
extern void FM_$WRITE(void *uid_info, uint32_t vtoce_ptr, uint16_t segment,
                      void *data, uint8_t flags, status_$t *status);

/* BAT (Block Allocation Table) functions */
extern void BAT_$RESERVE(uint8_t vol_idx, uint32_t count, status_$t *status);
extern void BAT_$ALLOCATE(uint8_t vol_idx, uint32_t hint, uint32_t count_flags,
                          uint32_t *addrs, status_$t *status);

/* DISK functions - see disk/disk.h for declarations */
#include "disk/disk.h"

/* NETWORK functions */
extern void NETWORK_$GET_NET(uint32_t node, void *net_info, status_$t *status);
/* Note: NETWORK_$AST_GET_INFO declared in ast.h */
extern int16_t NETWORK_$READ_AHEAD(void *net_info, void *uid, uint32_t *ppn_array,
                                    uint16_t page_size, int16_t count,
                                    int8_t no_read_ahead, uint8_t flags,
                                    int32_t *dtm, clock_t *clock,
                                    uint32_t *acl_info, status_$t *status);

/* NETBUF functions */
extern void NETBUF_$RTN_DAT(uint32_t addr);
extern void NETBUF_$GET_DAT(uint32_t *buffer);

/* Note: NETLOG_$OK_TO_LOG and NETLOG_$LOG_IT declared in ast.h */

/* PMAP functions needed by AST (pmap.h excluded due to NETLOG signature conflict) */
extern void PMAP_$WAKE_PURIFIER(int8_t wait);
extern uint16_t PMAP_$LOW_THRESH;
/* PMAP global variables at 0xE232xx */
extern uint32_t DAT_00e232b4;       /* Available pages pool 1 */
extern uint32_t DAT_00e232d8;       /* Available pages pool 2 */
extern uint32_t DAT_00e232fc;       /* Available pages pool 3 */

/* REM_FILE (Remote File) functions */
extern void REM_FILE_$SET_ATTRIBUTE(void *net_info, uid_t *uid, uint16_t attr_type,
                                     void *value, status_$t *status);

/* Internal AST functions not yet identified */
extern void FUN_00e01bee(void *uid_info, status_$t *status);
extern void FUN_00e01c52(void *uid_info, uint32_t *vol_ptr, void *attrs, status_$t *status);
extern void FUN_00e01950(aste_t *aste, uint32_t flags, status_$t *status);
extern void FUN_00e04b00(void);  /* Attribute setting helper */

/*
 * Internal helper functions
 */

/* Look up AOTE by UID - returns AOTE pointer */
aote_t *ast_$lookup_aote_by_uid(uid_t *uid);

/* Force lookup/activate AOTE for segment - returns AOTE pointer */
aote_t *ast_$force_activate_segment(uid_t *uid, uint16_t segment, status_$t *status, int8_t force);

/* Look up existing ASTE for AOTE/segment */
aste_t* ast_$lookup_aste(aote_t *aote, int16_t segment);

/* Look up or create ASTE for AOTE/segment */
aste_t* ast_$lookup_or_create_aste(aote_t *aote, uint16_t segment, status_$t *status);

/* Wait for page transition to complete */
void ast_$wait_for_page_transition(void);

/* Allocate pages - returns count, takes count_flags and ppn_array */
int16_t ast_$allocate_pages(uint32_t count_flags, uint32_t *ppn_array);

/* Clear transition bits in segment map */
void ast_$clear_transition_bits(uint32_t *segmap, uint16_t count);

/* Setup page read */
void ast_$setup_page_read(aste_t *aste, uint32_t *segmap, uint16_t start_page,
                          uint16_t count, uint16_t flags, status_$t *status);

/* Count valid pages and allocate for reading
 * Note: This is a nested procedure - accesses parent frame variables.
 * The signature here represents the flattened version for portability.
 */
int16_t ast_$count_valid_pages(aste_t *aste, int16_t count,
                                uint8_t per_boot_flag,
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

/* Set attribute on object */
void ast_$set_attribute_internal(uid_t *uid, uint16_t attr_type, void *value,
                                 int8_t wait_flag, void *exsid_info,
                                 clock_t *clock_info, status_$t *status);

/* Validate UID and return status */
status_$t ast_$validate_uid(uid_t *uid, uint32_t flags);

/*
 * Internal global variables
 */

/* Volume info count at 0xE1E0A0 (offset 0x420 from globals) */
extern uint16_t ast_$vol_info_count;
#define DAT_00e1e0a0 ast_$vol_info_count

/* Unknown at 0xE1E088 (offset 0x408) */
extern uint32_t ast_$unknown_e1e088;
#define DAT_00e1e088 ast_$unknown_e1e088

/* Volume index array at 0xE1E092 */
extern int16_t ast_$vol_indices[];
#define DAT_00e1e092 ast_$vol_indices

/* Clobbered UID storage at 0xE1E110 (offset 0x490) */
extern uid_t ast_$clobbered_uid;
#define DAT_00e1e110 ast_$clobbered_uid

/* Dismount failed AOTE pointer */
extern aote_t* AST_$DISMOUNT_FAILED_PTR;

/* Set trouble callback pointer */
extern void *PTR_AST_$SET_TROUBLE_00e07272;

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
