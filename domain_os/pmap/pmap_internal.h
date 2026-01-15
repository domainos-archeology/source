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
 * FUN_00e1360c - Batch write helper
 * Original address: 0x00e1360c
 */
void FUN_00e1360c(void);

/*
 * FUN_00e1359c - Segment map helper
 * Original address: 0x00e1359c
 */
void FUN_00e1359c(uint16_t *segmap_entry, uint32_t vpn, uint16_t page_idx);

/*
 * FUN_00e12e5e - Page flush helper
 * Original address: 0x00e12e5e
 */
void FUN_00e12e5e(uint32_t vpn, status_$t *status, int8_t sync_flag);

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

/*
 * FUN_00e12d84 - VPN/offset helper
 * Original address: 0x00e12d84
 */
void FUN_00e12d84(int16_t vpn, int16_t offset);

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
