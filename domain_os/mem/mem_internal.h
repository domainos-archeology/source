/*
 * mem/mem_internal.h - Memory support subsystem internal API
 *
 * Internal declarations shared only by the MEM subsystem's implementation
 * files.  Every non-test .c file in mem/ includes this header first.
 */

#ifndef MEM_INTERNAL_H
#define MEM_INTERNAL_H

#include "mem/mem.h"

/*
 * Per-page parity error tracking entry (MEM_$PAGE_ERRORS, 0xE22942).
 * Each entry is 18 bytes:
 *   - 4 bytes: Physical address
 *   - 2 bytes: Error count
 *   - 12 bytes: Reserved/padding
 */
typedef struct {
    uint32_t    phys_addr;      /* Physical address of failing page */
    uint16_t    error_count;    /* Number of errors at this address */
    uint8_t     reserved[12];   /* Padding to 18 bytes */
} mem_$page_error_t;

#endif /* MEM_INTERNAL_H */
