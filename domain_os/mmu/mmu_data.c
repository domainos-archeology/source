/*
 * mmu_data.c - MMU Module Global Data Definitions
 *
 * This file defines the global variables used by the MMU module
 * for non-M68K architectures.
 *
 * Original M68K addresses:
 *   MMU_$PID_PRIV:         0xE23D2C (2 bytes) - PID and privilege bits
 *   M68020:                0xE23D2E (2 bytes) - Non-zero if 68020+
 *   VA_TO_PTT_OFFSET_MASK: 0xE23D30 (4 bytes) - VA to PTT offset mask
 *   MMU_$VA_SHIFT:         0xE23D34 (2 bytes) - VA shift count
 *   MMU_$PTT_SHIFT:        0xE23D36 (2 bytes) - PTT shift count
 *   MCR_SHADOW:            0xE242D2 (1 byte)  - MCR shadow register
 *   MMU_SYSREV:            0xE2426F (1 byte)  - MMU hardware revision
 *
 * Memory-mapped hardware (M68K addresses - not data variables):
 *   PTT (Page Translation Table):   0x700000
 *   PFT (Page Frame Table):         0xFFB800
 *   ASID Table:                     0xEC2800
 *   MMU Control Registers:          0xFFB400-0xFFB409
 */

#include "mmu/mmu.h"

#if !defined(ARCH_M68K)

/*
 * ============================================================================
 * Hardware Base Pointers
 * ============================================================================
 *
 * These pointers must be initialized by platform-specific startup code
 * to point to the appropriate memory-mapped locations.
 */

/* Page Translation Table - indexed by virtual address */
uint16_t *mmu_ptt_base = (void*)0;

/* Page Frame Table - 4 bytes per physical page */
uint32_t *mmu_pft_base = (void*)0;

/* ASID table - 2 bytes per physical page */
uint16_t *mmu_asid_table_base = (void*)0;

/*
 * ============================================================================
 * MMU Control Registers
 * ============================================================================
 *
 * These pointers must be initialized by platform-specific startup code.
 */

volatile uint16_t *mmu_csr = (void*)0;           /* PID/Priv/Power */
volatile uint16_t *mmu_power_reg = (void*)0;     /* Power control */
volatile uint8_t  *mmu_status_reg = (void*)0;    /* Status */
volatile uint8_t  *mmu_mcr_m68010 = (void*)0;    /* MCR for 68010 */
volatile uint8_t  *mmu_mcr_mask = (void*)0;      /* MCR mask */
volatile uint8_t  *mmu_mcr_m68020 = (void*)0;    /* MCR for 68020 */
volatile uint8_t  *mmu_hw_rev = (void*)0;        /* Hardware revision */

/*
 * ============================================================================
 * MMU State Variables
 * ============================================================================
 */

/*
 * Processor type flag
 *
 * Non-zero if running on 68020 or later processor.
 * Used to select between 68010 and 68020 code paths.
 *
 * Original address: 0xE23D2E
 */
uint16_t mmu_m68020 = 0;

/*
 * PID and privilege bits
 *
 * Current address space ID and privilege level.
 * Written to MMU CSR when switching contexts.
 *
 * Original address: 0xE23D2C
 */
uint16_t mmu_pid_priv = 0;

/*
 * Virtual address to PTT offset mask
 *
 * Mask for extracting PTT index from virtual address.
 * Set to 0x3FFC00 on 68020+, 0x0FFC00 on 68010.
 *
 * Original address: 0xE23D30
 */
uint32_t mmu_va_to_ptt_mask = 0x0FFC00;

/*
 * MMU hardware revision
 *
 * Read from hardware during MMU_$SET_SYSREV.
 *
 * Original address: 0xE2426F
 */
uint8_t mmu_sysrev = 0;

/*
 * Current address space ID
 *
 * The ASID currently installed in the MMU.
 */
uint16_t mmu_current_asid = 0;

/*
 * MCR shadow register
 *
 * Shadow copy of Memory Control Register for 68010.
 * Allows reading current MCR value without hardware access.
 *
 * Original address: 0xE242D2
 */
uint8_t mmu_mcr_shadow = 0;

/*
 * ============================================================================
 * System Revision
 * ============================================================================
 */

/*
 * System revision code
 *
 * Combined hardware/software revision identifier.
 */
uint32_t MMU_$SYSTEM_REV = 0;

#endif /* !M68K */
