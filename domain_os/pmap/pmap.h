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

#include "../base/base.h"
#include "../ec/ec.h"

/*
 * Forward declarations
 */
struct aste_t;
struct aote_t;

/*
 * PMAP Global Variables
 */
#if defined(M68K)
    /* PMAP globals are at E24D44 + offset */
    #define PMAP_GLOBALS_BASE           0xE24D44

    /* Event counters */
    #define PMAP_$PAGES_EC              (*(ec_$eventcount_t*)0xE25494)    /* 0x750 */
    #define PMAP_$L_PURIFIER_EC         (*(ec_$eventcount_t*)0xE254AC)    /* 0x768 */
    #define PMAP_$R_PURIFIER_EC         (*(ec_$eventcount_t*)0xE254A0)    /* 0x75C */

    /* Thresholds */
    #define PMAP_$LOW_THRESH            (*(uint16_t*)0xE254E0)           /* Low threshold */
    #define PMAP_$MID_THRESH            (*(uint16_t*)0xE254E2)           /* Mid threshold */

    /* Scan interval */
    #define PMAP_$WS_INTERVAL           (*(uint16_t*)0xE254DE)           /* Working set interval */

    /* Statistics */
    #define PMAP_$T_PUR_SCANS           (*(uint32_t*)0xE254DC)           /* Timed purifier scans */

    /* Shutdown flag */
    #define PMAP_$SHUTTING_DOWN_FLAG    (*(int8_t*)0xE254E8)             /* Shutdown in progress */

    /* Current purifier slot */
    #define PMAP_$CURRENT_SLOT          (*(uint16_t*)0xE254E4)           /* 0x7A0 */

#else
    /* For non-m68k platforms */
    extern ec_$eventcount_t  pmap_pages_ec;
    extern ec_$eventcount_t  pmap_l_purifier_ec;
    extern ec_$eventcount_t  pmap_r_purifier_ec;
    extern uint16_t         pmap_low_thresh;
    extern uint16_t         pmap_mid_thresh;
    extern uint16_t         pmap_ws_interval;
    extern uint32_t         pmap_t_pur_scans;
    extern int8_t           pmap_shutting_down_flag;
    extern uint16_t         pmap_current_slot;

    #define PMAP_$PAGES_EC              pmap_pages_ec
    #define PMAP_$L_PURIFIER_EC         pmap_l_purifier_ec
    #define PMAP_$R_PURIFIER_EC         pmap_r_purifier_ec
    #define PMAP_$LOW_THRESH            pmap_low_thresh
    #define PMAP_$MID_THRESH            pmap_mid_thresh
    #define PMAP_$WS_INTERVAL           pmap_ws_interval
    #define PMAP_$T_PUR_SCANS           pmap_t_pur_scans
    #define PMAP_$SHUTTING_DOWN_FLAG    pmap_shutting_down_flag
    #define PMAP_$CURRENT_SLOT          pmap_current_slot
#endif

/*
 * External references to MMAP module
 */
extern uint32_t MMAP_$PAGEABLE_PAGES_LOWER_LIMIT;
extern uint32_t MMAP_$WSL[];             /* Working set list (36 bytes per entry) */
extern uint16_t MMAP_$WSL_HI_MARK[];     /* Working set list high water marks */

/*
 * External references to time module
 */
extern uint32_t TIME_$CLOCKH;            /* High-resolution clock */

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

/*
 * System functions
 */
extern void ML_$LOCK(uint16_t lock_id);
extern void ML_$UNLOCK(uint16_t lock_id);
/* EC_$ADVANCE, EC_$WAIT, EC_$WAITN, CRASH_SYSTEM declared in ec/ec.h */
extern void PROC1_$SET_LOCK(uint16_t lock_id);

/*
 * Time functions
 */
extern void TIME_$CLOCK(clock_t *clock);
extern void TIME_$ABS_CLOCK(clock_t *clock);
extern void TIME_$Q_ENTER_ELEM(int16_t queue, void *elem1, void *elem2, status_$t *status);
extern void TIME_$Q_REMOVE_ELEM(int16_t queue, int32_t elem, int16_t flags);
extern void TIME_$WAIT(void *time, void *ec, status_$t *status);

/*
 * MMAP functions
 */
extern void MMAP_$GET_IMPURE(uint16_t type, int *list, int8_t flag, uint16_t max,
                              uint32_t *result, uint16_t *count);
extern void MMAP_$UNAVAIL_REMOV(uint32_t ppn);
extern void MMAP_$AVAIL(uint32_t ppn);
extern void MMAP_$PURGE(uint16_t slot);
extern void MMAP_$WS_SCAN(uint16_t slot, uint16_t flags, uint32_t mask1, uint32_t mask2);
extern void MMAP_$SET_WS_INDEX(uint16_t index, int16_t *param);
extern void MMAP_$FREE_WSL(uint16_t index);

/*
 * MMU functions
 */
extern void MMU_$REMOVE(uint32_t ppn);

/*
 * AST functions
 */
extern void AST_$UPDATE(void);
extern ec_$eventcount_t AST_$PMAP_IN_TRANS_EC;   /* AST pmap in-transit event counter */

/*
 * DISK functions
 */
extern void DISK_$GET_QBLKS(uint16_t count, int32_t *main_qblk, int32_t *alt_qblks);
extern void DISK_$WRITE_MULTI(uint8_t flags, int32_t qblk, status_$t *status);
extern void DISK_$RTN_QBLKS(uint16_t count, int32_t main_qblk, int32_t alt_qblk);

/*
 * LOG functions
 */
extern int32_t LOG_$UPDATE(void);

/*
 * NETLOG functions
 */
extern void NETLOG_$LOG_IT(uint16_t type, uid_$t *uid, uint16_t count, uint16_t status,
                           int16_t disk1_hi, int16_t disk1_lo,
                           int16_t disk2_hi, int16_t disk2_lo);

/*
 * CAL functions
 */
extern void CAL_$SHUTDOWN(status_$t *status);

/*
 * Math helper (unsigned long divide)
 */
extern uint32_t M_DIU_LLW(uint32_t dividend, uint32_t divisor);

/*
 * Internal helper functions
 */
extern void FUN_00e12e5e(uint32_t ppn, status_$t *status, int8_t flags);
extern void FUN_00e1359c(uint16_t *segmap, uint32_t ppn, uint16_t page);
extern void FUN_00e1360c(void);

/*
 * Error strings
 */
extern const char status_$t_00e13a14[];
extern const char status_$t_00e145ec[];

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
