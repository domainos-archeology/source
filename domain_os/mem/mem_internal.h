/*
 * mem/mem_internal.h - Memory support subsystem internal API
 *
 * Internal declarations shared only by the MEM subsystem's implementation
 * files.  Every non-test .c file in mem/ includes this header first.
 *
 * The MEM_ A5 block (mem_data_t / MEM_DATA) and the two record types that
 * make it up live in mem/mem.h, because ASKNODE_$INTERNET_INFO copies
 * MEM_$MEM_REC out of it.
 */

#ifndef MEM_INTERNAL_H
#define MEM_INTERNAL_H

#include "mem/mem.h"

/*
 * Memory board boundary (3MB mark).
 *
 *   00e0adbe  cmpi.l #0x300000,(0x8,A6)
 *   00e0adc6  bcc.b  ...             ; unsigned: >= 0x300000 is board 2
 */
#define MEM_BOARD_BOUNDARY 0x300000

/*
 * Board numbers MEM_$BOARD_ERRORS is indexed by.  The array is 1-based (see
 * mem/mem.h), so these are the index values, not zero-based ordinals.
 */
#define MEM_BOARD_LOW  1  /* physical addresses <  MEM_BOARD_BOUNDARY */
#define MEM_BOARD_HIGH 2  /* physical addresses >= MEM_BOARD_BOUNDARY */

/*
 * Page-identity mask.  MEM_$PARITY_LOG compares only bits 21..16 of the
 * physical address, i.e. `and.b #0x3f` applied to byte 1 of the longword
 * (00e0ade2 `and.b (0x9,A6),D2b`, 00e0adf0 `and.b (0x1,A0),D3b`), so two
 * errors anywhere inside the same 64KB-aligned 4MB-wrapped region share a
 * record.  Written as a shift + mask so the host build agrees with m68k.
 */
#define MEM_PAGE_ID(phys_addr) ((uint16_t)(((phys_addr) >> 16) & 0x3F))

#endif /* MEM_INTERNAL_H */
