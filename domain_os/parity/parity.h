/*
 * PARITY - Memory Parity Error Handling Subsystem
 *
 * This module provides handling for memory parity errors in Domain/OS.
 * Parity errors indicate bit flips in RAM and can be caused by hardware
 * faults, cosmic rays, or electrical noise.
 *
 * The parity subsystem:
 * - Detects and logs parity errors
 * - Attempts to recover corrupted pages when possible
 * - Tracks error frequency by memory board
 * - Crashes the system for unrecoverable errors
 *
 * Hardware interface:
 * - MMU status register at 0xFFB403 indicates parity error conditions
 * - Memory error registers at 0xFFB404-0xFFB406 provide error details
 * - Different register layouts for SAU1 (68020) vs SAU2 (68010) systems
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef PARITY_H
#define PARITY_H

#include "base/base.h"

/*
 * Parity Error State Structure
 *
 * This structure tracks the current parity error being processed.  It is
 * the first cell of the FIM wired data island, map PARITY_$INFO 0xE21FE6
 * (PARITY_$CHK `movea.l #0xe21fe6,A2' at 0x00E0AE7E), so the one object is
 * FIM_$WIRED_DATA.parity (fim/fim.h); it is declared here, with its owner.
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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(parity_state_t, spurious_count) == 0x00, "parity_state_t.spurious_count");
_Static_assert(__builtin_offsetof(parity_state_t, chk_in_progress) == 0x02, "parity_state_t.chk_in_progress");
_Static_assert(__builtin_offsetof(parity_state_t, reserved_03) == 0x03, "parity_state_t.reserved_03");
_Static_assert(__builtin_offsetof(parity_state_t, err_ppn) == 0x04, "parity_state_t.err_ppn");
_Static_assert(__builtin_offsetof(parity_state_t, err_pa) == 0x08, "parity_state_t.err_pa");
_Static_assert(__builtin_offsetof(parity_state_t, err_va) == 0x0C, "parity_state_t.err_va");
_Static_assert(__builtin_offsetof(parity_state_t, err_status) == 0x10, "parity_state_t.err_status");
_Static_assert(__builtin_offsetof(parity_state_t, err_data) == 0x12, "parity_state_t.err_data");
_Static_assert(sizeof(parity_state_t) == 0x14, "parity_state_t: PARITY_$INFO..MISS_STATUS (0xE21FE6..0xE21FFA)");

/*
 * Parity error status codes (module 0x0E)
 */
extern status_$t Fault_Memory_Parity_Err;
extern status_$t Fault_Spurious_Parity_Err;

/*
 * PARITY_$CHK - Handle memory parity error
 *
 * Called from the parity error trap handler (FIM_$PARITY_TRAP) to
 * diagnose and handle a parity error. This function:
 *
 * 1. Validates the error is real (not spurious)
 * 2. Extracts the physical address from hardware registers
 * 3. Converts to virtual address via MMU_$PTOV
 * 4. Attempts to locate the corrupted data word
 * 5. Calls AST_$REMOVE_CORRUPTED_PAGE to handle the page
 * 6. Logs the error via MEM_$PARITY_LOG and LOG_$ADD
 * 7. Clears the error condition in hardware
 *
 * The function handles both SAU1 (68020-based) and SAU2 (68010-based)
 * systems, which have different memory error register layouts.
 *
 * If the error occurred during DMA, recovery is not possible.
 * If the page cannot be recovered, the system crashes.
 *
 * Returns:
 *   0xFF (-1): Error recovered successfully
 *   0x00: Error not recovered (requires further handling by caller)
 *
 * Original address: 0x00E0AE68
 * Size: 770 bytes
 */
int8_t PARITY_$CHK(void);

/*
 * PARITY_$CHK_IO - Check if I/O address matches parity error
 *
 * Called by I/O subsystems to check whether their buffer addresses
 * were involved in a parity error during DMA. This allows I/O
 * operations to detect and handle parity errors in their data.
 *
 * Parameters:
 *   ppn1 - First physical page number to check
 *   ppn2 - Second physical page number to check
 *
 * Returns:
 *   0: Neither address matches the parity error
 *   1: First address (ppn1) matches
 *   2: Second address (ppn2) matches
 *
 * If a match is found, the parity error state is cleared
 * (PARITY_$ERR_PPN set to 0, PARITY_$DURING_DMA cleared).
 *
 * Original address: 0x00E0B174
 * Size: 72 bytes
 */
uint32_t PARITY_$CHK_IO(uint32_t ppn1, uint32_t ppn2);

#endif /* PARITY_H */
