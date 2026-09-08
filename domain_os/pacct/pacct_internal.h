/*
 * PACCT Internal Header
 *
 * Internal data structures and helper functions for the process
 * accounting subsystem. This header should only be included by
 * pacct/ source files.
 */

#ifndef PACCT_INTERNAL_H
#define PACCT_INTERNAL_H

#include "pacct/pacct.h"
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
    /* 0x18: base of the mapped region.  MST_$UNMAP_PRIVI takes the start VA
     * BY VALUE (0x00E5A94C `move.l (0x18,A5),-(SP)`; the callee reads it as a
     * longword at (0x0E,A6), 0x00E448D6), so callers pass ARCH_PTR_TO_VA of
     * this field, not the field itself. */
    uint32_t   *map_ptr;
    uint32_t    file_pos;       /* 0x1C: Current file position/length */
} pacct_state_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(pacct_state_t, owner) == 0x00, "pacct_state_t.owner");
_Static_assert(__builtin_offsetof(pacct_state_t, lock_handle) == 0x08, "pacct_state_t.lock_handle");
_Static_assert(__builtin_offsetof(pacct_state_t, buf_remaining) == 0x0C, "pacct_state_t.buf_remaining");
_Static_assert(__builtin_offsetof(pacct_state_t, write_ptr) == 0x10, "pacct_state_t.write_ptr");
_Static_assert(__builtin_offsetof(pacct_state_t, map_offset) == 0x14, "pacct_state_t.map_offset");
_Static_assert(__builtin_offsetof(pacct_state_t, file_pos) == 0x1C, "pacct_state_t.file_pos");
_Static_assert(sizeof(pacct_state_t) == 0x20, "pacct_state_t size");
#endif

/*
 * Global accounting state
 */
extern pacct_state_t pacct_state;

/*
 * Short names for the fields of the one accounting state block, which the
 * image reaches as A5-relative displacements off 0x00E817EC:
 *
 *   (A5)      0x00E817EC  owner          PACCT_$ON 0x00E5A9AA
 *   (0x8,A5)  0x00E817F4  lock_handle    PACCT_$START 0x00E5A812
 *   (0xc,A5)  0x00E817F8  buf_remaining  PACCT_$LOG 0x00E5AC22
 *   (0x10,A5) 0x00E817FC  write_ptr      PACCT_$LOG 0x00E5ACA4
 *   (0x14,A5) 0x00E81800  map_offset     PACCT_$LOG 0x00E5AC6E
 *   (0x18,A5) 0x00E81804  map_ptr        PACCT_$SHUTDN 0x00E5A6EE
 *   (0x1c,A5) 0x00E81808  file_pos       PACCT_$LOG 0x00E5ACC4
 *
 * These replace the DAT_<address> spellings the decompiler produced (bead
 * source-ffh1); the SAU2 map names only the segment,
 * "D E817EC PACCT size = 20", not the fields inside it.
 */
#define pacct_owner         pacct_state.owner
#define pacct_lock_handle   pacct_state.lock_handle
#define pacct_buf_remaining pacct_state.buf_remaining
#define pacct_write_ptr     pacct_state.write_ptr
#define pacct_map_offset    pacct_state.map_offset
#define pacct_map_ptr       pacct_state.map_ptr
#define pacct_file_pos      pacct_state.file_pos

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
