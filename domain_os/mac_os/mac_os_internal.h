/*
 * MAC_OS - MAC Operating System Interface - Internal Header
 *
 * Internal definitions and helper functions for the MAC_OS subsystem.
 */

#ifndef MAC_OS_INTERNAL_H
#define MAC_OS_INTERNAL_H

#include "mac_os/mac_os.h"
#include "ml/ml.h"
#include "time/time.h"
#include "sock/sock.h"
#include "os/os.h"
#include "fim/fim.h"
#include "netbuf/netbuf.h"
#include "proc1/proc1.h"

/*
 * ============================================================================
 * Internal Constants
 * ============================================================================
 */

/* Packet type entry size in bytes */
#define MAC_OS_PKT_TYPE_ENTRY_SIZE  0x0C

/* Port packet type table size in bytes */
#define MAC_OS_PORT_PKT_TABLE_SIZE  0xF4

/* Channel entry size in bytes */
#define MAC_OS_CHANNEL_SIZE         0x14

/* Channel flags */
#define MAC_OS_FLAG_IN_USE          0x0200  /* Bit 9: Channel is in use */
#define MAC_OS_FLAG_OPEN            0x0002  /* Bit 1: Channel is open */
#define MAC_OS_FLAG_PROMISCUOUS     0x0001  /* Bit 0: Promiscuous mode */
#define MAC_OS_FLAG_ASID_MASK       0x00FC  /* Bits 2-7: AS_ID << 2 */
#define MAC_OS_FLAG_ASID_SHIFT      2

/*
 * ============================================================================
 * Route Port Structure Offsets
 * ============================================================================
 * The route_port_t structure is accessed via ROUTE_$PORTP[port_index].
 * These offsets are used to access fields within route_port_t.
 */

#define ROUTE_PORT_DRIVER_INFO_OFFSET   0x48    /* Pointer to driver info */
#define ROUTE_PORT_NET_TYPE_OFFSET      0x2E    /* Network type (2 bytes) */
#define ROUTE_PORT_LINE_NUM_OFFSET      0x30    /* Line number (2 bytes) */

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * MAC_OS_$CHECK_RANGE_OVERLAP - Check if packet type ranges overlap
 *
 * Checks if a new packet type range overlaps with any existing ranges
 * in the port's packet type table.
 *
 * Parameters:
 *   new_range  - Pointer to new range (8 bytes: low, high)
 *   table      - Pointer to packet type entry table
 *   count      - Number of existing entries
 *
 * Returns:
 *   0 if no overlap (OK to add)
 *   0xFF in low byte if overlap detected
 *
 * Original address: 0x00E0B1BC
 */
int16_t MAC_OS_$CHECK_RANGE_OVERLAP(uint32_t *new_range, mac_os_$pkt_type_entry_t *table, int16_t count);

/*
 * MAC_OS_$FIND_PACKET_TYPE - Find matching packet type entry
 *
 * Searches the port's packet type table for an entry that matches
 * the given packet type value.
 *
 * Parameters:
 *   pkt_type   - Packet type value to search for
 *   table      - Pointer to packet type entry table
 *   count      - Number of entries
 *
 * Returns:
 *   Index of matching entry (0-based)
 *   -1 if not found
 *
 * Original address: 0x00E0B202
 */
int16_t MAC_OS_$FIND_PACKET_TYPE(uint32_t pkt_type, mac_os_$pkt_type_entry_t *table, int16_t count);

/*
 * MAC_OS_$COPY_BUFFER_DATA - Copy data from buffer chain
 *
 * Copies data from a linked list of buffers into a destination buffer.
 * This is a nested Pascal procedure that accesses the caller's stack
 * frame to track buffer chain state.
 *
 * Parameters (passed on stack plus parent frame):
 *   dest_ptr   - Pointer to destination pointer (updated during copy)
 *   length     - Number of bytes to copy
 *
 * Parent frame variables accessed:
 *   offset -0x1c: Current buffer pointer
 *   offset -0x36: Current offset within buffer
 *
 * Original address: 0x00E0B522
 */
void MAC_OS_$COPY_BUFFER_DATA(int32_t *dest_ptr, int16_t length);

/*
 * MAC_OS_$NOP - No operation placeholder
 *
 * An empty function used as a placeholder during initialization.
 *
 * Original address: 0x00E0B244
 */
void MAC_OS_$NOP(void);

/*
 * ============================================================================
 * External Dependencies
 * ============================================================================
 * Note: External function declarations are provided by the included headers:
 *   - ML_$EXCLUSION_* from ml/ml.h
 *   - TIME_$ABS_CLOCK from time/time.h
 *   - SOCK_$CLOSE from sock/sock.h
 *   - OS_$DATA_COPY from os/os.h
 *   - FIM_$CLEANUP, FIM_$RLS_CLEANUP from fim/fim.h
 *   - NETBUF_$* from netbuf/netbuf.h
 *   - PROC1_$AS_ID from proc1/proc1.h
 */

#endif /* MAC_OS_INTERNAL_H */
