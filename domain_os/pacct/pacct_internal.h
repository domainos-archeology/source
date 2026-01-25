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
#include "file/file_internal.h"
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

#endif /* PACCT_INTERNAL_H */
