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
extern uint32_t DAT_00e23344;            /* Remote purifier flag */
extern uint32_t DAT_00e232d8;            /* Page count 1 */
extern uint32_t DAT_00e232fc;            /* Page count 2 */
extern uint32_t DAT_00e232b4;            /* Page count 3 */
extern uint32_t DAT_00e23320;            /* Impure pages flag */

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
void PMAP_$WS_SCAN_CALLBACK(int *param);

/*
 * Function prototypes - Working set management
 */
void PMAP_$INIT_WS_SCAN(uint16_t index, int16_t param);
void PMAP_$PURGE_WS(int16_t index, int16_t flags);

#endif /* PMAP_H */
