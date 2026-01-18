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
