/*
 * PARITY - Memory Parity Error Handling Subsystem (Internal Header)
 *
 * Internal data structures and globals for parity error handling.
 */

#ifndef PARITY_INTERNAL_H
#define PARITY_INTERNAL_H

#include "ast/ast.h"
#include "cache/cache.h"
#include "log/log.h"
#include "mem/mem.h"
#include "misc/crash_system.h"
#include "mmu/mmu.h"
#include "parity/parity.h"

/*
 * Parity Error State Structure
 *
 * This structure tracks the current parity error being processed.
 * Located at 0xE21FE6 on m68k systems.
 */
typedef struct parity_state_t {
  int16_t spurious_count; /* 0x00: Count of spurious parity errors */
  int8_t chk_in_progress; /* 0x02: -1 if parity check in progress */
  int8_t reserved_03;     /* 0x03: Padding */
  uint32_t err_ppn;       /* 0x04: Physical page number of error */
  uint32_t err_pa;        /* 0x08: Physical address of error */
  uint32_t err_va;        /* 0x0C: Virtual address of error */
  uint16_t err_status;    /* 0x10: Hardware status word */
  uint16_t err_data;      /* 0x12: Data word at error location */
} parity_state_t;

/*
 * Log entry structure for parity errors
 */
typedef struct parity_log_entry_t {
  uint16_t status;    /* 0x00: Hardware status word */
  uint32_t phys_addr; /* 0x02: Physical address */
  uint32_t virt_addr; /* 0x06: Virtual address */
} parity_log_entry_t;

/*
 * Memory Error Register Bit Definitions
 *
 * These constants describe the hardware register formats for memory
 * parity errors. The layout differs between SAU1 (68020-based) and
 * SAU2 (68010-based) systems.
 *
 * SAU1 (MMU type 1, bit 0 of MMU_STATUS_REG = 1):
 *   0xFFB404: Error status byte in bits 24-31 (accessed as long)
 *   0xFFB406: Error address bits (page frame << 4 in bits 4-15)
 *             Bits 0-3: byte lane indicators
 *             Bit 3: DMA error flag
 *
 * SAU2 (MMU type 2, bit 0 of MMU_STATUS_REG = 0):
 *   0xFFB404: Error status long
 *             Bits 28-31: unused
 *             Bits 12-27: Page frame number << 2
 *             Bit 5: DMA error upper
 *             Bit 4: DMA error lower
 *             Bits 0-3: byte lane indicators (0xF = no error)
 *   0xFFB406: Used to clear error by writing
 */

/* SAU1 error register bits */
#define SAU1_ERR_BYTE_UPPER 0x02 /* Upper byte parity error */
#define SAU1_ERR_BYTE_LOWER 0x04 /* Lower byte parity error */
#define SAU1_ERR_DMA 0x08        /* Error during DMA */
#define SAU1_PPN_MASK 0xFFF0     /* Page frame in bits 4-15 */
#define SAU1_PPN_SHIFT 4

/* SAU2 error register bits (in low byte of status long at 0xFFB404) */
#define SAU2_ERR_LANE_MASK 0x0F /* Byte lane error mask */
#define SAU2_ERR_NO_ERROR 0x0F  /* All lanes OK = no error */
#define SAU2_ERR_DMA_UPPER 0x20 /* Error during DMA (upper) */
#define SAU2_ERR_DMA_LOWER 0x10 /* Error during DMA (lower) */
#define SAU2_PPN_SHIFT 12       /* Page frame starts at bit 12 */

/* Byte-within-word determination */
#define ERR_BYTE_MASK 0x03    /* Low 2 bits of status */
#define ERR_BYTE_BOTH 0x03    /* Both bytes had error */
#define ERR_BYTE_EVEN_OK 0x0A /* Even byte OK (odd byte bad) */

/*
 * Architecture-specific definitions
 */
#if defined(ARCH_M68K)

/* Parity state global (at 0xE21FE6) */
#define PARITY_$STATE (*(parity_state_t *)0xE21FE6)

/* Individual field access for compatibility */
#define PARITY_$INFO (*(int16_t *)0xE21FE6) /* Spurious error count */
#define PARITY_$CHK_IN_PROGRESS                                                \
  (*(int8_t *)0xE21FE8)                            /* Check-in-progress flag */
#define PARITY_$ERR_PPN (*(uint32_t *)0xE21FEA)    /* Error page number */
#define PARITY_$ERR_PA (*(uint32_t *)0xE21FEE)     /* Error physical address */
#define PARITY_$ERR_VA (*(uint32_t *)0xE21FF2)     /* Error virtual address */
#define PARITY_$ERR_STATUS (*(uint16_t *)0xE21FF6) /* Hardware status */
#define PARITY_$ERR_DATA (*(uint16_t *)0xE21FF8)   /* Data at error */

/* Parity during DMA flag (at 0xE2298C) */
#define PARITY_$DURING_DMA (*(int8_t *)0xE2298C)

/* Memory Error Registers */
#define MEM_ERR_STATUS_LONG (*(volatile uint32_t *)0xFFB404)
#define MEM_ERR_STATUS_WORD (*(volatile uint16_t *)0xFFB406)

#else /* !M68K */

/* For non-m68k platforms, these will be provided by platform init */
extern parity_state_t parity_state;
extern int8_t parity_during_dma;
extern volatile uint32_t *mem_err_status_long;
extern volatile uint16_t *mem_err_status_word;

#define PARITY_$STATE parity_state
#define PARITY_$INFO parity_state.spurious_count
#define PARITY_$CHK_IN_PROGRESS parity_state.chk_in_progress
#define PARITY_$ERR_PPN parity_state.err_ppn
#define PARITY_$ERR_PA parity_state.err_pa
#define PARITY_$ERR_VA parity_state.err_va
#define PARITY_$ERR_STATUS parity_state.err_status
#define PARITY_$ERR_DATA parity_state.err_data
#define PARITY_$DURING_DMA parity_during_dma
#define MEM_ERR_STATUS_LONG (*mem_err_status_long)
#define MEM_ERR_STATUS_WORD (*mem_err_status_word)

#endif /* M68K */

/*
 * Memory parity log tracking (from MEM subsystem)
 *
 * Tracks parity errors by memory board and by page.
 * Located at 0xE22930 on m68k.
 */

/* Number of per-page error tracking records */
#define MEM_PARITY_PAGE_RECORDS 4

/* Memory parity record for tracking errors per page */
typedef struct mem_parity_record_t {
  uint32_t phys_addr;   /* 0x00: Physical address */
  uint16_t count;       /* 0x04: Error count for this page */
  uint8_t reserved[12]; /* 0x06: Padding to 0x12 bytes */
} mem_parity_record_t;

/* Memory parity log globals structure */
typedef struct mem_parity_log_t {
  uint16_t reserved_00[4]; /* 0x00: Reserved */
  uint16_t board1_count;   /* 0x08: Errors on board 1 (< 0x300000) */
  uint16_t board2_count;   /* 0x0A: Errors on board 2 (>= 0x300000) */
  uint16_t reserved_0c[3]; /* 0x0C: Reserved */
  mem_parity_record_t
      records[MEM_PARITY_PAGE_RECORDS]; /* 0x12: Per-page records */
} mem_parity_log_t;

#if defined(ARCH_M68K)
#define MEM_PARITY_LOG (*(mem_parity_log_t *)0xE22930)
#define MEM_BOARD1_COUNT (*(uint16_t *)0xE2293A)
#define MEM_BOARD2_COUNT (*(uint16_t *)0xE2293C)
#define MEM_PARITY_RECORDS ((mem_parity_record_t *)0xE22942)
#else
extern mem_parity_log_t mem_parity_log;
#define MEM_PARITY_LOG mem_parity_log
#define MEM_BOARD1_COUNT mem_parity_log.board1_count
#define MEM_BOARD2_COUNT mem_parity_log.board2_count
#define MEM_PARITY_RECORDS mem_parity_log.records
#endif

/* Memory board boundary (3MB mark) */
#define MEM_BOARD_BOUNDARY 0x300000

/*
 * MEM_$PARITY_LOG - Log a parity error
 *
 * Called to record a parity error in the memory parity tracking table.
 * Tracks errors by memory board and by page, replacing lowest-count
 * entries when the table is full.
 *
 * Parameters:
 *   phys_addr - Physical address where parity error occurred
 *
 * Original address: 0x00E0ADB0
 * Size: 182 bytes
 */
void MEM_$PARITY_LOG(uint32_t phys_addr);

/*
 * Scratch page for parity error recovery
 *
 * A temporary page at 0xFF9000 is used to re-read data during
 * parity error diagnosis. This page is installed via MMU_$INSTALL
 * to allow reading the corrupted page without triggering another fault.
 */
#if defined(ARCH_M68K)
#define PARITY_SCRATCH_PAGE ((uint16_t *)0xFF9000)
#else
extern uint16_t *parity_scratch_page;
#define PARITY_SCRATCH_PAGE parity_scratch_page
#endif

/* Protection value for scratch page installation */
#define PARITY_SCRATCH_PROT 0x16 /* Supervisor read/write */

#endif /* PARITY_INTERNAL_H */
