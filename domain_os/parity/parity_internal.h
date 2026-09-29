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
#include "fim/fim.h"        /* FIM_$WIRED_DATA.parity: the parity state cells */


/*
 * Log entry structure for parity errors
 */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct parity_log_entry_t {
  uint16_t status;    /* 0x00: Hardware status word */
  uint32_t phys_addr; /* 0x02: Physical address */
  uint32_t virt_addr; /* 0x06: Virtual address */
} __attribute__((packed)) parity_log_entry_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(parity_log_entry_t, status) == 0x00, "parity_log_entry_t.status");
_Static_assert(__builtin_offsetof(parity_log_entry_t, phys_addr) == 0x02, "parity_log_entry_t.phys_addr");
_Static_assert(__builtin_offsetof(parity_log_entry_t, virt_addr) == 0x06, "parity_log_entry_t.virt_addr");

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
/*
 * PARITY_$DURING_DMA - "the latched parity error happened during DMA" flag,
 * a Domain boolean (-1 / 0).  The map's one-cell segment "D E2298C PARITY
 * size = 4" (after MEM_ 0xE22930..0xE2298C) is the PARITY module's A5 base:
 * PARITY_$CHK (0x00E0AE70) and PARITY_$CHK_IO (0x00E0B17A) both `lea
 * (0xe2298c).l,A5' and touch the flag as `(A5)' (0x00E0AF06 / 0x00E0AF58
 * `move.b Dn,(A5)', 0x00E0B1B2 `clr.b (A5)').  Defined in
 * parity/parity_data.c as a plain object for now; TODO(source-ppgz): make
 * it the MODULE_DATA block PARITY_$DATA like the other A5 blocks.
 */
extern int8_t PARITY_$DURING_DMA;

#if defined(ARCH_M68K)

/* Memory Error Registers */
#define MEM_ERR_STATUS_LONG (*(volatile uint32_t *)0xFFB404)
#define MEM_ERR_STATUS_WORD (*(volatile uint16_t *)0xFFB406)

#else /* !M68K */

/* For non-m68k platforms, these will be provided by platform init */
extern volatile uint32_t *mem_err_status_long;
extern volatile uint16_t *mem_err_status_word;

#define MEM_ERR_STATUS_LONG (*mem_err_status_long)
#define MEM_ERR_STATUS_WORD (*mem_err_status_word)

#endif /* M68K */

/*
 * The MEM_ module's parity-log cells are the one A5 block MEM_DATA
 * (0xE22930..0xE2298C, the SAU2 map's `D E22930 MEM_ size = 5C` segment); it
 * is declared in mem/mem.h along with MEM_$SIZE, MEM_$MEM_REC,
 * MEM_$BOARD_ERRORS and MEM_$PAGE_ERRORS, and its only writer is
 * MEM_$PARITY_LOG (mem/parity_log.c) -- beads source-3uo, source-eqom.
 */
/* MEM_$PARITY_LOG is declared in mem/mem.h (included above) */

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
