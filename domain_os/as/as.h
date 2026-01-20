/*
 * AS_$ - Address Space Subsystem
 *
 * This subsystem manages virtual address space layout information for
 * Domain/OS processes. It provides constants and functions for querying
 * the boundaries of various address space regions:
 *
 * Address Space Regions:
 * - Private A: Process-private memory (low addresses)
 * - Global A: Shared memory region A
 * - Private B: Additional process-private memory
 * - Global B: Shared memory region B
 *
 * The address space layout differs between M68010 and M68020 systems,
 * with M68020 systems having larger address spaces due to the larger
 * virtual address range.
 *
 * Reverse engineered from Domain/OS
 */

#ifndef AS_H
#define AS_H

#include "base/base.h"

/*
 * Address space region identifiers for AS_$GET_ADDR
 */
#define AS_REGION_PRIVATE_A  0  /* Private A region */
#define AS_REGION_GLOBAL_A   1  /* Global A region */
#define AS_REGION_PRIVATE_B  2  /* Private B region */
#define AS_REGION_GLOBAL_B   3  /* Global B region */

/*
 * Address range structure returned by AS_$GET_ADDR
 * Contains the base address and size of an address space region
 */
typedef struct as_$addr_range {
    uint32_t base;    /* Base address of region */
    uint32_t size;    /* Size of region in bytes */
} as_$addr_range_t;

/*
 * AS_$ Info Structure
 *
 * This structure contains all address space layout information.
 * Located at 0xE2B914 in the kernel data segment.
 * Total size is 92 bytes (0x5C).
 *
 * The structure is initialized with default values for M68010 systems,
 * then AS_$INIT adjusts certain fields for M68020 systems.
 *
 * Field layout based on AS_$INIT assembly analysis:
 */
typedef struct as_$info {
    uint16_t reserved_00;           /* 0x00: Reserved/unused */
    uint16_t reserved_02;           /* 0x02: Reserved/unused */
    uint32_t global_a;              /* 0x04: Global A base address */
    uint32_t global_a_size;         /* 0x08: Global A size */
    uint32_t m68020_global_a;       /* 0x0C: Global A (M68020 copy) */
    uint32_t m68020_global_a_size;  /* 0x10: Global A size (M68020 copy) */
    uint32_t private_base;          /* 0x14: Private region base */
    uint32_t stack_file_low;        /* 0x18: Stack file low boundary */
    uint32_t cr_rec;                /* 0x1C: CR record address */
    uint32_t reserved_20;           /* 0x20: Reserved */
    uint32_t cr_rec_end;            /* 0x24: CR record end address */
    uint32_t reserved_28;           /* 0x28: Reserved */
    uint32_t stack_file_high;       /* 0x2C: Stack file high boundary */
    uint32_t reserved_30;           /* 0x30: Reserved */
    uint32_t stack_low;             /* 0x34: Stack low boundary */
    uint32_t reserved_38;           /* 0x38: Reserved */
    uint32_t stack_high;            /* 0x3C: Stack high boundary */
    uint32_t reserved_40;           /* 0x40: Reserved */
    uint32_t stack_offset;          /* 0x44: Stack offset */
    uint32_t reserved_48;           /* 0x48: Reserved */
    uint32_t init_stack_file_size;  /* 0x4C: Initial stack file size */
    uint32_t cr_rec_file;           /* 0x50: CR record file address */
    uint32_t reserved_54;           /* 0x54: Reserved */
    uint32_t cr_rec_file_size;      /* 0x58: CR record file size */
} as_$info_t;

/*
 * Size of AS info structure in bytes
 * Used by AS_$GET_INFO for bounds checking
 */
#define AS_INFO_SIZE  92  /* 0x5C bytes */

/*
 * Global variables
 */
extern as_$info_t AS_$INFO;         /* Address space info structure at 0xE2B914 */
extern int16_t AS_$INFO_SIZE;       /* Size of info area at 0xE2B970 */
extern int16_t AS_$PROTECTION;      /* Protection flags at 0xE2B972 */

/* Convenience aliases to structure fields */
#define AS_$GLOBAL_A        AS_$INFO.global_a
#define AS_$PRIVATE         AS_$INFO.private_base
#define AS_$STACK_FILE_LOW  AS_$INFO.stack_file_low
#define AS_$CR_REC          AS_$INFO.cr_rec
#define AS_$STACK_LOW       AS_$INFO.stack_low
#define AS_$STACK_HIGH      AS_$INFO.stack_high
#define AS_$STACK_OFFSET    AS_$INFO.stack_offset

/*
 * Function prototypes
 */

/*
 * AS_$INIT - Initialize address space configuration
 *
 * Adjusts address space layout for M68020 systems.
 * On M68010 systems, does nothing (uses default values).
 * On M68020 systems, adjusts Global A and stack addresses for the
 * larger address space.
 *
 * Called during system initialization.
 */
void AS_$INIT(void);

/*
 * AS_$GET_INFO - Get address space information
 *
 * Copies the AS info structure to the caller's buffer.
 * The amount copied is the minimum of the requested size and
 * AS_$INFO_SIZE.
 *
 * Parameters:
 *   buffer      - Destination buffer for info
 *   req_size    - Pointer to requested size (in bytes)
 *   actual_size - Pointer to receive actual size copied
 */
void AS_$GET_INFO(void *buffer, int16_t *req_size, int16_t *actual_size);

/*
 * AS_$GET_ADDR - Get address range for a region
 *
 * Returns the base address and size of the specified address
 * space region.
 *
 * Parameters:
 *   addr_range - Pointer to receive base and size
 *   region     - Region identifier (AS_REGION_*)
 *
 * For invalid region values, returns base=0x7FFFFFFF, size=0.
 */
void AS_$GET_ADDR(as_$addr_range_t *addr_range, int16_t *region);

#endif /* AS_H */
