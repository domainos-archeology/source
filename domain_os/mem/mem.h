/*
 * MEM - Memory Management Support Module
 *
 * This module provides low-level memory management support functions
 * for Domain/OS. It includes memory error tracking, memory pool
 * management, and other memory-related utilities.
 *
 * This module is distinct from:
 * - MMU: Hardware MMU interface
 * - MMAP: Memory map management
 * - AST: Active Segment Table management
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef MEM_H
#define MEM_H

#include "base/base.h"

/*
 * MEM_$PARITY_LOG - Log a memory parity error
 *
 * Records a parity error in the memory parity tracking table.
 * The table tracks:
 *   - Total errors per memory board (board 1 < 3MB, board 2 >= 3MB)
 *   - Per-page error counts for the most frequently failing pages
 *
 * When a new page has an error and the per-page table is full,
 * the entry with the lowest count is replaced.
 *
 * Parameters:
 *   phys_addr - Physical address where parity error occurred
 *
 * Original address: 0x00E0ADB0
 * Size: 182 bytes
 */
void MEM_$PARITY_LOG(uint32_t phys_addr);

#endif /* MEM_H */
