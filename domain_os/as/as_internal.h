/*
 * as/as_internal.h - Address Space subsystem internal API
 *
 * Internal declarations shared only by the AS subsystem's implementation
 * files.  Every non-test .c file in as/ includes this header first.
 */

#ifndef AS_INTERNAL_H
#define AS_INTERNAL_H

#include "as/as.h"
#include "mst/mst.h"
#include "mmu/mmu.h"

/*
 * Segment size shift value (AS_$GET_ADDR)
 * Each MST segment is 32KB = 0x8000 = 2^15 bytes, so a segment number
 * is shifted left by 15 to get its address.
 */
#define SEGMENT_SHIFT  15

/*
 * M68020 address space adjustment offset (AS_$INIT)
 * Applied to stack and CR record addresses on M68020 systems
 */
#define M68020_AS_OFFSET  0x2A00000

/*
 * M68020 Global A configuration (AS_$INIT)
 */
#define M68020_GLOBAL_A_BASE  0x33C0000
#define M68020_GLOBAL_A_SIZE  0x700000

#endif /* AS_INTERNAL_H */
