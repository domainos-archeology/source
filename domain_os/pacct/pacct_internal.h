/*
 * PACCT Internal Header
 *
 * Internal data structures and helper functions for the process
 * accounting subsystem. This header should only be included by
 * pacct/ source files.
 */

#ifndef PACCT_INTERNAL_H
#define PACCT_INTERNAL_H

#include "pacct.h"
#include "uid/uid.h"
#include "time/time.h"
#include "cal/cal.h"
#include "acl/acl.h"
#include "file/file.h"
#include "rgyc/rgyc.h"
#include "mst/mst.h"

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/*
 * Process accounting state block
 * Size: 32 bytes (0x20)
 * Location: 0xE817EC (m68k)
 *
 * Contains all state for the accounting subsystem.
 */
typedef struct pacct_state_t {
    uid_t       owner;          /* 0x00: Accounting file UID (UID_$NIL = disabled) */
    uint32_t    lock_handle;    /* 0x08: File lock handle */
    uint32_t    buf_remaining;  /* 0x0C: Bytes remaining in mapped buffer */
    uint32_t   *write_ptr;      /* 0x10: Current write pointer in buffer */
    uint32_t    map_offset;     /* 0x14: Current mapping offset in file */
    uint32_t   *map_ptr;        /* 0x18: Base of mapped region */
    uint32_t    file_pos;       /* 0x1C: Current file position/length */
} pacct_state_t;

/*
 * Global accounting state
 */
extern pacct_state_t pacct_state;

/* Aliases for individual fields (matching Ghidra decompiler output) */
#define pacct_owner         pacct_state.owner
#define DAT_00e817f4        pacct_state.lock_handle
#define DAT_00e817f8        pacct_state.buf_remaining
#define DAT_00e817fc        pacct_state.write_ptr
#define DAT_00e81800        pacct_state.map_offset
#define DAT_00e81804        pacct_state.map_ptr
#define DAT_00e81808        pacct_state.file_pos

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * pacct_$compress - Compress a value to comp_t format
 *
 * Compresses a 32-bit value to 16-bit comp_t format:
 *   - If value > 0x1FFF, shift right by 3 and increment exponent
 *   - Round up if bit 2 was set before shift
 *   - Exponent stored in bits 13-15, mantissa in bits 0-12
 *
 * Parameters:
 *   value - Value to compress
 *
 * Returns:
 *   Compressed value in comp_t format
 *
 * Original address: 0x00E5A9CA
 */
comp_t pacct_$compress(uint32_t value);

/*
 * pacct_$clock_to_comp - Convert clock value to compressed format
 *
 * Converts a 48-bit clock_t value to compressed format:
 *   1. Divide by 0x1047 (approximately 4167, the timer constant)
 *   2. Compress the result
 *
 * This converts from 4-microsecond ticks to a time unit suitable
 * for accounting.
 *
 * Parameters:
 *   clock - Pointer to clock value to convert
 *
 * Returns:
 *   Compressed time value
 *
 * Original address: 0x00E5AA28
 */
comp_t pacct_$clock_to_comp(clock_t *clock);

/*
 * ============================================================================
 * External Dependencies
 * ============================================================================
 */

/*
 * MST_$UNMAP_PRIVI - Unmap a privileged memory region
 *
 * Parameters:
 *   flags      - Unmap flags
 *   uid        - UID for mapping context
 *   map_ptr    - Pointer to mapped region
 *   map_offset - Mapping offset
 *   unused     - Unused parameter
 *   status_ret - Output status
 *
 * Original address: 0x00E448B0
 */
void MST_$UNMAP_PRIVI(int16_t flags, uid_t *uid, void *map_ptr,
                      uint32_t map_offset, int16_t unused, status_$t *status_ret);

/*
 * MST_$MAPS - Map a segment with specified options
 *
 * Parameters:
 *   flags        - Map flags
 *   access_mode  - Access mode (read/write)
 *   file_uid     - UID of file to map
 *   file_offset  - Offset in file
 *   length       - Length to map
 *   prot         - Protection flags
 *   unused1      - Unused
 *   unused2      - Unused
 *   map_addr_ret - Output: mapped address
 *   status_ret   - Output: status code
 *
 * Returns map pointer in A0.
 *
 * Original address: 0x00E43982
 */
void *MST_$MAPS(int16_t flags, int16_t access_mode, uid_t *file_uid,
                uint32_t file_offset, uint32_t length, int16_t prot,
                int32_t unused1, int8_t unused2, uint32_t *map_addr_ret,
                status_$t *status_ret);

/*
 * FILE_$PRIV_LOCK - Lock a file with privileges
 *
 * Extended file locking with callback support.
 *
 * Parameters:
 *   file_uid    - UID of file to lock
 *   unused1     - Unused
 *   lock_mode   - Lock mode (1 = exclusive)
 *   access      - Access mode
 *   unused2     - Unused
 *   flags       - Lock flags
 *   unused3     - Unused
 *   unused4     - Unused
 *   unused5     - Unused
 *   callback    - Callback function
 *   unused6     - Unused
 *   handle_ret  - Output: lock handle
 *   status_buf  - Status buffer
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E5F0EE
 */
uint32_t FILE_$PRIV_LOCK(uid_t *file_uid, int16_t unused1, int16_t lock_mode,
                         int16_t access, int16_t unused2, uint32_t flags,
                         int32_t unused3, int32_t unused4, int32_t unused5,
                         void *callback, int16_t unused6, uint32_t *handle_ret,
                         void *status_buf, status_$t *status_ret);

/*
 * FILE_$PRIV_UNLOCK - Unlock a file with privileges
 *
 * Parameters:
 *   file_uid   - UID of file to unlock
 *   handle     - Lock handle from PRIV_LOCK
 *   flags      - Unlock flags
 *   unused1-3  - Unused
 *   status_buf - Status buffer
 *   status_ret - Output: status code
 *
 * Original address: 0x00E5FD32
 */
uint32_t FILE_$PRIV_UNLOCK(uid_t *file_uid, int16_t handle, uint32_t flags,
                           int32_t unused1, int32_t unused2, int32_t unused3,
                           void *status_buf, status_$t *status_ret);

/*
 * FILE_$SET_LEN - Set file length
 *
 * Parameters:
 *   file_uid   - UID of file
 *   length     - Pointer to new length
 *   status_ret - Output: status code
 *
 * Original address: 0x00E73F90
 */
void FILE_$SET_LEN(uid_t *file_uid, uint32_t *length, status_$t *status_ret);

/*
 * ============================================================================
 * Math Helper Functions
 * ============================================================================
 */

/*
 * M$OIU$WLW - Unsigned 32-bit multiply, return high 16 bits
 *
 * Multiplies a 32-bit value by a 16-bit value, returns high 16 bits
 * of the 48-bit result.
 *
 * Parameters:
 *   multiplicand - 32-bit value
 *   multiplier   - 16-bit value
 *
 * Returns:
 *   High 16 bits of (multiplicand * multiplier)
 *
 * Original address: 0x00E0ACC0
 */
uint16_t M$OIU$WLW(uint32_t multiplicand, uint16_t multiplier);

/*
 * M$DIU$LLW - Unsigned 32-bit divide by 16-bit
 *
 * Divides a 32-bit value by a 16-bit value.
 *
 * Parameters:
 *   dividend - 32-bit value
 *   divisor  - 16-bit value
 *
 * Returns:
 *   32-bit quotient
 *
 * Original address: 0x00E0AC68
 */
uint32_t M$DIU$LLW(uint32_t dividend, uint16_t divisor);

#endif /* PACCT_INTERNAL_H */
