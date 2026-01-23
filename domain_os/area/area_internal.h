/*
 * AREA Internal Header
 *
 * Internal declarations for the AREA subsystem.
 * This header should only be included by area/*.c files.
 */

#ifndef AREA_INTERNAL_H
#define AREA_INTERNAL_H

#include "area/area.h"
#include "cal/cal.h"
#include "ml/ml.h"
#include "network/network.h"
#include "proc1/proc1.h"
#include "rem_file/rem_file.h"

/*
 * External references for network support
 */
extern int8_t NETWORK_$DISKLESS;
extern uint32_t NETWORK_$MOTHER_NODE;

/*
 * AS_$STACK_LOW - Stack low address
 * Used for copy operations to avoid copying stack region
 */
extern uint32_t AS_$STACK_LOW;

/*
 * Error message for internal crashes
 */
extern status_$t Area_Internal_Error;

/*
 * Extended segment table entry for areas with > 16 segments
 * Used by AREA_$COPY and segment threading operations
 */
typedef struct area_$seg_table_t {
    int16_t area_id;            /* 0x00: Area ID */
    uint8_t table_index;        /* 0x02: Table index (0-255) */
    uint8_t pad;                /* 0x03: Padding */
    struct area_$seg_table_t *next;  /* 0x04: Next in ASID list */
    uint32_t *bitmap_ptr;       /* 0x08: Pointer to extended bitmap */
} area_$seg_table_t;

/*
 * AREA per-ASID extended segment table list
 * Array at AREA_GLOBALS_BASE + 0x68, indexed by ASID
 */
#define AREA_SEG_TABLE_LIST_BASE    (AREA_GLOBALS_BASE + 0x68)

/*
 * area_$alloc_resources - Allocate area resources
 *
 * Attempts to allocate backing store resources for an area.
 *
 * @param timeout       Timeout value (0x60 = normal)
 *
 * Returns: negative if failed, non-negative if successful
 *
 * Original address: 0x00E075CA
 */
int8_t area_$alloc_resources(int16_t timeout);

/*
 * area_$remote_sync - Sync with remote partner
 *
 * Synchronizes area state with remote partner node.
 *
 * Original address: 0x00E087BA
 */
void area_$remote_sync(void);

/*
 * area_$free_segments - Free segment range
 *
 * Frees segments within the specified range.
 *
 * @param area_id       Area ID
 * @param start_seg     Starting segment
 * @param end_seg       Ending segment
 * @param clear_bitmap  If true, clear bitmap entries
 * @param status_p      Output: status code
 *
 * Original address: 0x00E085A6
 */
void area_$free_segments(int16_t area_id, uint32_t start_seg,
                          uint32_t end_seg, int8_t clear_bitmap,
                          status_$t *status_p);

/*
 * area_$lookup_seg_table - Look up extended segment table
 *
 * @param asid          Address space ID
 * @param area_id       Area ID
 * @param table_idx     Table index
 *
 * Returns: Pointer to segment table entry, or NULL
 *
 * Original address: 0x00E09D2E
 */
area_$seg_table_t *area_$lookup_seg_table(int16_t asid, int16_t area_id,
                                           int16_t table_idx);

/*
 * area_$free_seg_table - Free extended segment table entry
 *
 * @param entry         Entry to free
 * @param prev          Previous entry in list
 * @param asid          Address space ID
 *
 * Original address: 0x00E09E48
 */
void area_$free_seg_table(area_$seg_table_t *entry,
                           area_$seg_table_t *prev, int16_t asid);

/*
 * area_$get_aste - Get ASTE for segment
 *
 * @param area_id       Area ID
 * @param bitmap_ptr    Pointer to segment bitmap
 * @param seg_idx       Segment index
 * @param param_4       Unknown parameter
 * @param param_5       Unknown parameter
 * @param status_p      Output: status code
 *
 * Returns: ASTE pointer in A0
 *
 * Original address: 0x00E09A6A
 */
void *area_$get_aste(int16_t area_id, void *bitmap_ptr, int16_t seg_idx,
                      int16_t param_4, int8_t param_5, status_$t *status_p);

/*
 * area_$find_entry_by_uid - Find area by UID in hash table
 *
 * @param handle_ptr    Pointer to area handle
 * @param seg_idx_ptr   Pointer to segment index
 * @param param_3       Unknown parameter
 * @param status_p      Output: status code
 *
 * Returns: ASTE pointer in A0
 *
 * Original address: 0x00E0939C
 */
void *area_$find_entry_by_uid(int16_t area_id, int16_t *seg_idx_ptr,
                               int16_t param_3, status_$t *status_p);

#endif /* AREA_INTERNAL_H */
