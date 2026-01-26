/*
 * pmap_data.c - PMAP Module Global Data Definitions
 *
 * This file defines the global variables used by the PMAP (Page Map)
 * module for page purification and working set management.
 *
 * Original M68K addresses:
 *   PMAP_$IDLE_INTERVAL:       0xE25484 (4 bytes)  - Idle interval (0x063c = 1596)
 *   PMAP_$T_PUR_SCANS:         0xE25488 (4 bytes)  - Timer purifier scans
 *   PMAP_$PUR_R_CNT:           0xE2548C (4 bytes)  - Remote purifier count
 *   PMAP_$PUR_L_CNT:           0xE25490 (4 bytes)  - Local purifier count
 *   PMAP_$PAGES_EC:            0xE25494 (12 bytes) - Pages eventcount
 *   PMAP_$R_PURIFIER_EC:       0xE254A0 (12 bytes) - Remote purifier EC
 *   PMAP_$L_PURIFIER_EC:       0xE254AC (12 bytes) - Local purifier EC
 *   PMAP_$SCAN_FRACT:          0xE254CC (2 bytes)  - Scan fraction (0xFFFF)
 *   PMAP_$MID_THRESH:          0xE254CE (2 bytes)  - Middle threshold
 *   PMAP_$LOW_THRESH:          0xE254D0 (2 bytes)  - Low threshold (1)
 *   PMAP_$WS_SCAN_DELTA:       0xE254D2 (2 bytes)  - Working set scan delta
 *   PMAP_$MIN_WS_INTERVAL:     0xE254D4 (2 bytes)  - Min WS interval (1)
 *   PMAP_$MAX_WS_INTERVAL:     0xE254D6 (2 bytes)  - Max WS interval (8)
 *   PMAP_$WS_INTERVAL:         0xE254D8 (2 bytes)  - Working set interval (5)
 *   PMAP_$SHUTTING_DOWN_FLAG:  0xE254DA (1 byte)   - Shutdown flag
 *   PMAP_$CURRENT_SLOT:        0xE254E4 (2 bytes)  - Current scan slot
 */

#include "pmap/pmap_internal.h"

/*
 * ============================================================================
 * Timer and Interval Configuration
 * ============================================================================
 */

/*
 * Idle interval for purifier sleep
 *
 * Time (in ticks) the purifier sleeps when idle.
 * Default: 0x063c = 1596 ticks
 *
 * Original address: 0xE25484
 */
uint32_t PMAP_$IDLE_INTERVAL = 0x063c;

/*
 * Timer purifier scan count
 *
 * Number of scans performed by the timer-based purifier.
 *
 * Original address: 0xE25488
 */
uint32_t PMAP_$T_PUR_SCANS = 0;

/*
 * Remote purifier page count
 *
 * Number of pages processed by the remote purifier.
 *
 * Original address: 0xE2548C
 */
uint32_t PMAP_$PUR_R_CNT = 0;

/*
 * Local purifier page count
 *
 * Number of pages processed by the local purifier.
 *
 * Original address: 0xE25490
 */
uint32_t PMAP_$PUR_L_CNT = 0;

/*
 * ============================================================================
 * Eventcounts
 * ============================================================================
 */

/*
 * Pages available eventcount
 *
 * Signaled when pages become available for allocation.
 *
 * Original address: 0xE25494
 */
ec_$eventcount_t PMAP_$PAGES_EC = { 0 };

/*
 * Remote purifier eventcount
 *
 * Used to wake the remote purifier process.
 *
 * Original address: 0xE254A0
 */
ec_$eventcount_t PMAP_$R_PURIFIER_EC = { 0 };

/*
 * Local purifier eventcount
 *
 * Used to wake the local purifier process.
 *
 * Original address: 0xE254AC
 */
ec_$eventcount_t PMAP_$L_PURIFIER_EC = { 0 };

/*
 * ============================================================================
 * Thresholds and Scan Parameters
 * ============================================================================
 */

/*
 * Scan fraction
 *
 * Fraction of pages to scan per iteration (0xFFFF = all).
 *
 * Original address: 0xE254CC
 */
uint16_t PMAP_$SCAN_FRACT = 0xFFFF;

/*
 * Middle threshold
 *
 * Page count threshold for medium-priority purging.
 *
 * Original address: 0xE254CE
 */
uint16_t PMAP_$MID_THRESH = 0;

/*
 * Low threshold
 *
 * Page count threshold for high-priority purging.
 * Default: 1
 *
 * Original address: 0xE254D0
 */
uint16_t PMAP_$LOW_THRESH = 1;

/*
 * Working set scan delta
 *
 * Change in working set size per scan.
 *
 * Original address: 0xE254D2
 */
uint16_t PMAP_$WS_SCAN_DELTA = 0;

/*
 * Minimum working set interval
 *
 * Minimum time between working set scans.
 * Default: 1
 *
 * Original address: 0xE254D4
 */
uint16_t PMAP_$MIN_WS_INTERVAL = 1;

/*
 * Maximum working set interval
 *
 * Maximum time between working set scans.
 * Default: 8
 *
 * Original address: 0xE254D6
 */
uint16_t PMAP_$MAX_WS_INTERVAL = 8;

/*
 * Current working set interval
 *
 * Current time between working set scans.
 * Default: 5
 *
 * Original address: 0xE254D8
 */
uint16_t PMAP_$WS_INTERVAL = 5;

/*
 * ============================================================================
 * State Flags
 * ============================================================================
 */

/*
 * Shutdown in progress flag
 *
 * Set to 0xFF when system shutdown begins.
 * Purifiers check this to stop processing.
 *
 * Original address: 0xE254DA
 */
int8_t PMAP_$SHUTTING_DOWN_FLAG = 0;

/*
 * Current scan slot
 *
 * Index into page table for current scan position.
 * Ranges from 5 to 69.
 * Default: 5
 *
 * Original address: 0xE254E4
 */
uint16_t PMAP_$CURRENT_SLOT = 5;
